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
  int out_fd = -1;
  int err_fd = -1;
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

std::string privileged_helper_path() {
  if (const char* override = g_getenv("LCOS_PRIV_HELPER")) {
    if (override[0] != '\0') {
      return override;
    }
  }
  const char* installed = "/usr/libexec/lcos-settings-priv";
  if (g_file_test(installed, G_FILE_TEST_IS_EXECUTABLE)) {
    return installed;
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

PrivSession::PrivSession() : token_(make_token()) {}

PrivSession::~PrivSession() {
  if (async_ != nullptr) {
    cancel();
    wait_until_idle();
  }
}

PrivResult PrivSession::start(const std::vector<std::string>& args, const std::string& input, bool async,
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
  const std::string helper = privileged_helper_path();
  if (!g_file_test(helper.c_str(), G_FILE_TEST_IS_EXECUTABLE)) {
    result.error = "The privileged helper is not installed.";
    if (done) {
      done(result);
    }
    return result;
  }
  std::vector<std::string> stored = {"pkexec", helper, token_};
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
  ::close(standard_in);
  set_nonblock(standard_out);
  set_nonblock(standard_err);
  busy_ = true;
  cancel_requested_ = false;

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
    if (result.ok && args[0] != "commit" && args[0] != "revert") {
      dirty_ = true;
    }
    return result;
  }

  async_ = new AsyncOp();
  async_->pid = pid;
  async_->out_fd = standard_out;
  async_->err_fd = standard_err;
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
    PrivResult parsed;
    if (session->cancel_requested_) {
      parsed.ok = false;
      parsed.error = "Cancelled.";
    } else {
      const HelperExit exit = interpret_helper_exit(0, status, session->async_->err);
      parsed.ok = exit.ok;
      parsed.error = exit.error;
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
  delete async_;
  async_ = nullptr;
  busy_ = false;
  if (result.ok) {
    dirty_ = true;
  }
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
  // The helper is in this process group and catches SIGTERM so it can put
  // locale.gen back before it exits.
  kill(-async_->pid, SIGTERM);
  if (async_->kill_src != 0) {
    return;
  }
  async_->kill_src = g_timeout_add(2000, [](gpointer data) -> gboolean {
    auto* session = static_cast<PrivSession*>(data);
    if (session->async_ != nullptr && session->async_->pid != 0) {
      kill(-session->async_->pid, SIGKILL);
      session->async_->kill_src = 0;
    }
    return G_SOURCE_REMOVE;
  }, this);
}

void PrivSession::wait_until_idle() {
  while (busy_) {
    g_main_context_iteration(nullptr, TRUE);
  }
}

PrivResult PrivSession::commit() {
  if (finished_ || !dirty_) {
    return {true, {}};
  }
  const PrivResult result = run({"commit"});
  if (result.ok) {
    finished_ = true;
    dirty_ = false;
  }
  return result;
}

PrivResult PrivSession::revert() {
  if (finished_ || !dirty_) {
    return {true, {}};
  }
  const PrivResult result = run({"revert"});
  if (result.ok) {
    finished_ = true;
    dirty_ = false;
  }
  return result;
}

}  // namespace lcos
