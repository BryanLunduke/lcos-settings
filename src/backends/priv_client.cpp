// SPDX-License-Identifier: GPL-3.0-or-later

#include "priv_client.hpp"

#include "logic/helper_status.hpp"
#include "logic/system_model.hpp"

#include <glib-unix.h>
#include <glib.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <random>
#include <utility>

namespace lcos {
namespace {

std::string make_token() {
  std::random_device device;
  std::uniform_int_distribution<unsigned> dist(0, 15);
  std::string token(32, '0');
  for (char& c : token) {
    const unsigned value = dist(device);
    c = static_cast<char>(value < 10 ? '0' + value : 'a' + (value - 10));
  }
  return token;
}

void make_group_leader(void*) { setpgid(0, 0); }

bool set_nonblock(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void append_read(int fd, std::string& out) {
  char buffer[4096];
  while (true) {
    const ssize_t n = ::read(fd, buffer, sizeof(buffer));
    if (n > 0) {
      out.append(buffer, buffer + n);
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
}

}  // namespace

struct PrivSession::AsyncOp {
  GPid pid = 0;
  int in_fd = -1;
  int out_fd = -1;
  int err_fd = -1;
  std::string command;
  std::string err;
  guint out_src = 0;
  guint err_src = 0;
  guint child_src = 0;
  guint kill_src = 0;
  bool out_eof = false;
  bool err_eof = false;
  bool reaped = false;
  int status = 0;
  std::function<void(PrivResult)> done;
};

std::string domain_name(PrivDomain domain) {
  if (domain == PrivDomain::Accounts) {
    return "accounts";
  }
  if (domain == PrivDomain::Locale) {
    return "locale";
  }
  return "system";
}

// Installed wrappers are what pkexec authorizes. A development build falls
// back to the helper binary and passes --domain itself.
std::string privileged_helper_path(PrivDomain domain, bool* wrapper) {
  if (wrapper != nullptr) {
    *wrapper = false;
  }
  if (const char* override = g_getenv("LCOS_PRIV_HELPER")) {
    if (override[0] != '\0') {
      return override;
    }
  }
  const std::string installed =
      std::string("/usr/libexec/lcos-settings-priv-") + domain_name(domain);
  if (g_file_test(installed.c_str(), G_FILE_TEST_IS_EXECUTABLE)) {
    if (wrapper != nullptr) {
      *wrapper = true;
    }
    return installed;
  }
  const char* plain = "/usr/libexec/lcos-settings-priv";
  if (g_file_test(plain, G_FILE_TEST_IS_EXECUTABLE)) {
    return plain;
  }
  gchar* exe = g_file_read_link("/proc/self/exe", nullptr);
  std::string path = "lcos-settings-priv";
  if (exe != nullptr) {
    gchar* dir = g_path_get_dirname(exe);
    path = std::string(dir) + "/lcos-settings-priv";
    g_free(dir);
    g_free(exe);
  }
  return path;
}

std::string privileged_helper_path() { return privileged_helper_path(PrivDomain::System, nullptr); }

PrivSession::PrivSession() : token_(make_token()) {}

PrivSession::~PrivSession() {
  if (async_ != nullptr) {
    cancel();
    wait_until_idle();
  }
}

void PrivSession::note_success(PrivDomain domain, const std::string& action, bool ok,
                               const std::string& error) {
  if (action == "commit" || action == "revert") {
    return;
  }
  const bool locale_failed = action == "set-locale" && !ok && error != "Cancelled." &&
                             error.find("Authentication") == std::string::npos;
  if (!ok && !locale_failed) {
    return;
  }
  if (domain == PrivDomain::Accounts) {
    touched_accounts_ = true;
  } else if (domain == PrivDomain::Locale) {
    touched_locale_ = true;
  } else {
    touched_system_ = true;
  }
  dirty_ = true;
  changed.emit();
}

PrivResult PrivSession::start(const std::vector<std::string>& args, const std::string& input, bool async,
                              std::function<void(PrivResult)> done) {
  const PrivDomain domain = args.empty() ? PrivDomain::System : priv_domain_for_action(args[0]);
  return run_domain(domain, args, input, async, std::move(done));
}

PrivResult PrivSession::run_domain(PrivDomain domain, const std::vector<std::string>& args,
                                   const std::string& input, bool async,
                                   std::function<void(PrivResult)> done) {
  PrivResult result;
  if (busy_ || finished_ || !valid_undo_token(token_) || args.empty()) {
    result.error = finished_ ? "This change can no longer be made in this visit."
                             : busy_ ? "Another privileged change is still running."
                                     : "This change can no longer be made in this visit.";
    if (done) {
      done(result);
    }
    return result;
  }
  bool wrapper = false;
  const std::string helper = privileged_helper_path(domain, &wrapper);
  if (!g_file_test(helper.c_str(), G_FILE_TEST_IS_EXECUTABLE)) {
    result.error = "The privileged helper is not installed.";
    if (done) {
      done(result);
    }
    return result;
  }
  // Wrappers inject --domain before exec. The bare helper needs it on argv.
  std::vector<std::string> stored = {"pkexec", helper};
  if (!wrapper) {
    stored.push_back("--domain=" + domain_name(domain));
  }
  stored.push_back(token_);
  stored.insert(stored.end(), args.begin(), args.end());
  std::vector<char*> argv;
  for (const auto& arg : stored) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);

  gint standard_in = -1;
  gint standard_out = -1;
  gint standard_err = -1;
  GError* error = nullptr;
  GPid pid = 0;
  const gboolean spawned = g_spawn_async_with_pipes(
      nullptr, argv.data(), nullptr,
      static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD), make_group_leader,
      nullptr, &pid, &standard_in, &standard_out, &standard_err, &error);
  if (!spawned || error != nullptr || pid == 0) {
    result.error = error != nullptr ? error->message : "Could not start the privileged helper.";
    if (error != nullptr) {
      g_error_free(error);
    }
    if (done) {
      done(result);
    }
    return result;
  }

  if (!input.empty()) {
    std::size_t wrote = 0;
    while (wrote < input.size()) {
      const ssize_t n = ::write(standard_in, input.data() + wrote, input.size() - wrote);
      if (n < 0) {
        if (errno == EINTR) {
          continue;
        }
        break;
      }
      wrote += static_cast<std::size_t>(n);
    }
  }
  // Sync helpers read stdin to EOF. The async locale helper keeps the pipe
  // so the desktop user can write a cancel line after pkexec is root.
  if (!async) {
    ::close(standard_in);
    standard_in = -1;
  }
  set_nonblock(standard_out);
  set_nonblock(standard_err);
  busy_ = true;
  cancel_requested_ = false;
  active_domain_ = domain;

  if (!async) {
    std::string err;
    std::string ignored_out;
    auto drain = [](int& fd, std::string& dest, short revents) {
      if (fd < 0 || revents == 0) {
        return;
      }
      if ((revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        append_read(fd, dest);
      }
      if ((revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
        ::close(fd);
        fd = -1;
      }
    };
    while (standard_out >= 0 || standard_err >= 0) {
      pollfd fds[2];
      fds[0].fd = standard_out;
      fds[0].events = POLLIN;
      fds[0].revents = 0;
      fds[1].fd = standard_err;
      fds[1].events = POLLIN;
      fds[1].revents = 0;
      const int ready = poll(fds, 2, -1);
      if (ready < 0) {
        if (errno == EINTR) {
          continue;
        }
        break;
      }
      drain(standard_out, ignored_out, fds[0].revents);
      drain(standard_err, err, fds[1].revents);
    }
    int status = 0;
    int wait_result = -1;
    while (true) {
      wait_result = waitpid(pid, &status, 0);
      if (wait_result < 0 && errno == EINTR) {
        continue;
      }
      break;
    }
    g_spawn_close_pid(pid);
    busy_ = false;
    result = [&]() {
      PrivResult parsed;
      const HelperExit exit = interpret_helper_exit(wait_result, status, err);
      parsed.ok = exit.ok;
      parsed.error = exit.error;
      return parsed;
    }();
    note_success(domain, args[0], result.ok, result.error);
    return result;
  }

  async_ = new AsyncOp();
  async_->pid = pid;
  async_->in_fd = standard_in;
  async_->out_fd = standard_out;
  async_->err_fd = standard_err;
  async_->command = args[0];
  async_->done = std::move(done);
  PrivSession* self = this;
  async_->out_src = g_unix_fd_add(standard_out, GIOCondition(G_IO_IN | G_IO_HUP | G_IO_ERR),
                                  [](gint fd, GIOCondition condition, gpointer data) -> gboolean {
                                    auto* session = static_cast<PrivSession*>(data);
                                    if (session->async_ == nullptr) {
                                      return G_SOURCE_REMOVE;
                                    }
                                    std::string ignored;
                                    append_read(fd, ignored);
                                    if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
                                      session->async_->out_eof = true;
                                      session->async_->out_src = 0;
                                      ::close(session->async_->out_fd);
                                      session->async_->out_fd = -1;
                                      return G_SOURCE_REMOVE;
                                    }
                                    return G_SOURCE_CONTINUE;
                                  },
                                  self);
  async_->err_src = g_unix_fd_add(standard_err, GIOCondition(G_IO_IN | G_IO_HUP | G_IO_ERR),
                                  [](gint fd, GIOCondition condition, gpointer data) -> gboolean {
                                    auto* session = static_cast<PrivSession*>(data);
                                    if (session->async_ == nullptr) {
                                      return G_SOURCE_REMOVE;
                                    }
                                    append_read(fd, session->async_->err);
                                    if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
                                      session->async_->err_eof = true;
                                      session->async_->err_src = 0;
                                      ::close(session->async_->err_fd);
                                      session->async_->err_fd = -1;
                                      return G_SOURCE_REMOVE;
                                    }
                                    return G_SOURCE_CONTINUE;
                                  },
                                  self);
  async_->child_src = g_child_watch_add(pid, [](GPid child, gint status, gpointer data) {
    auto* session = static_cast<PrivSession*>(data);
    if (session->async_ == nullptr) {
      g_spawn_close_pid(child);
      return;
    }
    session->async_->reaped = true;
    session->async_->status = status;
    session->async_->child_src = 0;
    g_spawn_close_pid(child);
    if (session->async_->out_fd >= 0) {
      std::string ignored;
      append_read(session->async_->out_fd, ignored);
      ::close(session->async_->out_fd);
      session->async_->out_fd = -1;
    }
    if (session->async_->err_fd >= 0) {
      append_read(session->async_->err_fd, session->async_->err);
      ::close(session->async_->err_fd);
      session->async_->err_fd = -1;
    }
    if (session->async_->out_src != 0) {
      g_source_remove(session->async_->out_src);
      session->async_->out_src = 0;
    }
    if (session->async_->err_src != 0) {
      g_source_remove(session->async_->err_src);
      session->async_->err_src = 0;
    }
    if (session->async_->in_fd >= 0) {
      ::close(session->async_->in_fd);
      session->async_->in_fd = -1;
    }
    PrivResult parsed;
    const HelperExit helper_exit = interpret_helper_exit(0, status, session->async_->err);
    // A cancel that never got past polkit stays an authentication result.
    // A helper that already finished successfully is not reported as cancelled.
    if (session->cancel_requested_ && !helper_exit.ok &&
        helper_exit.error.find("Authentication") == std::string::npos) {
      parsed.ok = false;
      parsed.error = "Cancelled.";
    } else {
      parsed.ok = helper_exit.ok;
      parsed.error = helper_exit.error;
    }
    session->finish_async(parsed);
  }, self);
  result.ok = true;
  return result;
}

void PrivSession::finish_async(const PrivResult& result) {
  if (async_ == nullptr) {
    busy_ = false;
    return;
  }
  if (async_->kill_src != 0) {
    g_source_remove(async_->kill_src);
    async_->kill_src = 0;
  }
  auto done = std::move(async_->done);
  const std::string command = async_->command;
  if (async_->in_fd >= 0) {
    ::close(async_->in_fd);
    async_->in_fd = -1;
  }
  delete async_;
  async_ = nullptr;
  busy_ = false;
  note_success(active_domain_, command, result.ok, result.error);
  if (done) {
    done(result);
  }
}

PrivResult PrivSession::run(const std::vector<std::string>& args, const std::string& input) {
  return start(args, input, false, {});
}

void PrivSession::run_async(const std::vector<std::string>& args, const std::string& input,
                            std::function<void(PrivResult)> done) {
  start(args, input, true, std::move(done));
}

void PrivSession::cancel() {
  if (async_ == nullptr || async_->pid == 0) {
    return;
  }
  cancel_requested_ = true;
  // Before authentication, pkexec is still the desktop user's process and
  // SIGTERM stops it. After authentication the helper is root and this
  // signal is EPERM. The cancel line is delivered on the pipe the user
  // owns; the helper kills locale-gen and restores locale.gen itself.
  if (async_->in_fd >= 0) {
    const char message[] = "cancel\n";
    const ssize_t n = ::write(async_->in_fd, message, sizeof(message) - 1);
    (void)n;
  }
  kill(-async_->pid, SIGTERM);
}

void PrivSession::wait_until_idle() {
  while (busy_) {
    g_main_context_iteration(nullptr, TRUE);
  }
}

PrivResult PrivSession::finish_domains(const char* action, bool end_session) {
  if (finished_ || !dirty_) {
    return {true, {}};
  }
  auto step = [&](bool touched, PrivDomain domain) -> PrivResult {
    if (!touched) {
      return {true, {}};
    }
    const PrivResult result = run_domain(domain, {action}, {}, false, {});
    if (!result.ok) {
      return result;
    }
    if (domain == PrivDomain::Accounts) {
      touched_accounts_ = false;
    } else if (domain == PrivDomain::Locale) {
      touched_locale_ = false;
    } else {
      touched_system_ = false;
    }
    return result;
  };
  // Strict domains first. Each one only removes its own undo files.
  PrivResult result = step(touched_accounts_, PrivDomain::Accounts);
  if (!result.ok) {
    return result;
  }
  result = step(touched_locale_, PrivDomain::Locale);
  if (!result.ok) {
    return result;
  }
  result = step(touched_system_, PrivDomain::System);
  if (!result.ok) {
    return result;
  }
  dirty_ = touched_accounts_ || touched_locale_ || touched_system_;
  if (end_session && !dirty_) {
    finished_ = true;
  }
  changed.emit();
  return {true, {}};
}

PrivResult PrivSession::commit() { return finish_domains("commit", true); }

PrivResult PrivSession::checkpoint() { return finish_domains("commit", false); }

PrivResult PrivSession::revert() { return finish_domains("revert", true); }

}  // namespace lcos
