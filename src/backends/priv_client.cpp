// SPDX-License-Identifier: GPL-3.0-or-later

#include "priv_client.hpp"

#include "logic/system_model.hpp"

#include <glib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <random>

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

}  // namespace

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

PrivResult PrivSession::run(const std::vector<std::string>& args, const std::string& input) {
  PrivResult result;
  if (finished_ || !valid_undo_token(token_) || args.empty()) {
    result.error = "This change can no longer be made in this visit.";
    return result;
  }
  const std::string helper = privileged_helper_path();
  if (!g_file_test(helper.c_str(), G_FILE_TEST_IS_EXECUTABLE)) {
    result.error = "The privileged helper is not installed.";
    return result;
  }
  std::vector<std::string> stored = {"pkexec", helper, token_};
  stored.insert(stored.end(), args.begin(), args.end());
  std::vector<char*> argv;
  for (const auto& arg : stored) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);

  gint standard_in = 0;
  gint standard_out = 0;
  gint standard_err = 0;
  GError* error = nullptr;
  const GPid pid = [&]() -> GPid {
    GPid child = 0;
    g_spawn_async_with_pipes(nullptr, argv.data(), nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr,
                             &child, &standard_in, &standard_out, &standard_err, &error);
    return child;
  }();
  if (error != nullptr || pid == 0) {
    result.error = error != nullptr ? error->message : "Could not start the privileged helper.";
    if (error != nullptr) {
      g_error_free(error);
    }
    return result;
  }
  if (!input.empty()) {
    std::size_t wrote = 0;
    while (wrote < input.size()) {
      const gssize n =
          write(standard_in, input.data() + wrote, input.size() - wrote);
      if (n < 0) {
        break;
      }
      wrote += static_cast<std::size_t>(n);
    }
  }
  close(standard_in);
  std::string err;
  char buffer[512];
  while (true) {
    const gssize n = read(standard_err, buffer, sizeof(buffer));
    if (n <= 0) {
      break;
    }
    err.append(buffer, buffer + n);
  }
  close(standard_out);
  close(standard_err);
  int status = 0;
  waitpid(pid, &status, 0);
  g_spawn_close_pid(pid);
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    result.ok = true;
    if (args[0] != "commit" && args[0] != "revert") {
      dirty_ = true;
    }
    return result;
  }
  while (!err.empty() && (err.back() == '\n' || err.back() == '\r')) {
    err.pop_back();
  }
  const auto nl = err.find('\n');
  if (nl != std::string::npos) {
    err.resize(nl);
  }
  result.error = err.empty() ? "The change was not applied." : err;
  return result;
}

PrivResult PrivSession::commit() {
  if (!dirty_ || finished_) {
    finished_ = true;
    dirty_ = false;
    return {true, {}};
  }
  const PrivResult result = run({"commit"});
  finished_ = true;
  dirty_ = false;
  return result.ok ? result : PrivResult{true, {}};
}

PrivResult PrivSession::revert() {
  if (!dirty_ || finished_) {
    finished_ = true;
    dirty_ = false;
    return {true, {}};
  }
  const PrivResult result = run({"revert"});
  finished_ = true;
  dirty_ = false;
  return result;
}

}  // namespace lcos
