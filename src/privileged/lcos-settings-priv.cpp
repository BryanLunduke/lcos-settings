// SPDX-License-Identifier: GPL-3.0-or-later
// Root-only helper for LCOS Command Center. pkexec starts it. Arguments are
// checked before any change. Passwords arrive on stdin and are never logged
// or placed on a command line.

#include "logic/atomic_file.hpp"
#include "logic/system_model.hpp"

#include <crypt.h>
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <shadow.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <ctime>
#include <vector>

namespace {

constexpr const char* kUndoRoot = "/run/lcos-settings";

void fail(const std::string& message) {
  std::cerr << message << "\n";
  std::exit(1);
}

std::string read_stdin() {
  std::ostringstream buffer;
  buffer << std::cin.rdbuf();
  return buffer.str();
}

std::string first_line(const std::string& text) {
  const auto cut = text.find('\n');
  return cut == std::string::npos ? text : text.substr(0, cut);
}

std::string second_line(const std::string& text) {
  const auto cut = text.find('\n');
  if (cut == std::string::npos) {
    return {};
  }
  const auto next = text.find('\n', cut + 1);
  return text.substr(cut + 1, next == std::string::npos ? std::string::npos : next - cut - 1);
}

bool run_command(const std::vector<std::string>& args, const std::string& input) {
  if (args.empty()) {
    return false;
  }
  int pipes[2] = {-1, -1};
  if (!input.empty() && pipe(pipes) != 0) {
    return false;
  }
  const pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    if (!input.empty()) {
      dup2(pipes[0], STDIN_FILENO);
      close(pipes[0]);
      close(pipes[1]);
    }
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  if (!input.empty()) {
    close(pipes[0]);
    std::size_t wrote = 0;
    while (wrote < input.size()) {
      const ssize_t n = write(pipes[1], input.data() + wrote, input.size() - wrote);
      if (n < 0) {
        break;
      }
      wrote += static_cast<std::size_t>(n);
    }
    close(pipes[1]);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::string undo_dir(const std::string& token) {
  return std::string(kUndoRoot) + "/" + token;
}

void ensure_undo(const std::string& token) {
  if (!lcos::valid_undo_token(token)) {
    fail("invalid request");
  }
  if (mkdir(kUndoRoot, 0700) != 0 && errno != EEXIST) {
    fail("cannot store undo information");
  }
  const std::string dir = undo_dir(token);
  if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
    fail("cannot store undo information");
  }
}

std::string undo_path(const std::string& token, const std::string& name) {
  return undo_dir(token) + "/" + name;
}

bool undo_exists(const std::string& token, const std::string& name) {
  return access(undo_path(token, name).c_str(), F_OK) == 0;
}

void remember_text(const std::string& token, const std::string& name, const std::string& text) {
  ensure_undo(token);
  if (undo_exists(token, name)) {
    return;
  }
  std::ofstream out(undo_path(token, name), std::ios::binary);
  if (!out) {
    fail("cannot store undo information");
  }
  out << text;
}

void remember_file(const std::string& token, const std::string& name, const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  if (!in && in.fail() && errno != 0 && access(path.c_str(), F_OK) == 0) {
    fail("cannot read the current setting");
  }
  remember_text(token, name, in ? buffer.str() : std::string());
  if (access(path.c_str(), F_OK) != 0) {
    remember_text(token, name + ".missing", "1");
  }
}

std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

void write_file(const std::string& path, const std::string& text, mode_t mode) {
  // The installer drop-in is never rewritten. A later file overrides it.
  if (path == "/etc/lightdm/lightdm.conf.d/50-lcos.conf") {
    fail("refusing to edit the installer LightDM file");
  }
  lcos::ReplaceRequest request;
  request.mode_if_new = mode;
  // LCOS links /etc/default/locale at ../locale.conf. Follow that link and
  // write the regular file it names. The symlink itself stays in place.
  // Any other symlink, including ~/.face, is still refused.
  if (path == "/etc/default/locale") {
    request.follow_symlink = true;
    request.symlink_targets = {"/etc/locale.conf", "/etc/default/locale"};
  }
  std::string error;
  if (!lcos::replace_regular_file(path, text, request, &error)) {
    fail(error.empty() ? "cannot write " + path : error);
  }
}

pid_t g_locale_child = -1;
volatile sig_atomic_t g_cancel_locale = 0;

void on_locale_term(int) {
  g_cancel_locale = 1;
  if (g_locale_child > 0) {
    // The child is its own process group, so this also stops localedef if
    // a shell wrapper spawned it.
    kill(-g_locale_child, SIGTERM);
  }
}

void arm_locale_cancel_pipe() {
  const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
  }
}

// Runs one program in its own process group. A cancel byte on stdin, which
// the desktop user can still write after pkexec is root, kills that group.
bool run_cancellable(const std::vector<std::string>& args) {
  if (args.empty() || g_cancel_locale != 0) {
    return false;
  }
  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);
  const pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    setpgid(0, 0);
    execv(argv[0], argv.data());
    _exit(127);
  }
  setpgid(pid, pid);
  g_locale_child = pid;
  bool watch_stdin = true;
  int term_tries = 0;
  int status = 0;
  while (true) {
    if (g_cancel_locale != 0) {
      kill(-pid, term_tries > 15 ? SIGKILL : SIGTERM);
      ++term_tries;
    }
    if (watch_stdin) {
      pollfd fd;
      fd.fd = STDIN_FILENO;
      fd.events = POLLIN;
      fd.revents = 0;
      const int ready = poll(&fd, 1, 200);
      if (ready > 0) {
        if ((fd.revents & POLLIN) != 0) {
          char buffer[128];
          const ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
          if (n > 0) {
            g_cancel_locale = 1;
            kill(-pid, SIGTERM);
          }
        }
        if ((fd.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0 && (fd.revents & POLLIN) == 0) {
          watch_stdin = false;
        }
      }
    }
    const pid_t got = waitpid(pid, &status, watch_stdin ? WNOHANG : 0);
    if (got == pid) {
      break;
    }
    if (got < 0 && errno != EINTR) {
      g_locale_child = -1;
      return false;
    }
  }
  g_locale_child = -1;
  if (g_cancel_locale != 0) {
    // The leader may have exited and left a child. Signal the group again.
    kill(-pid, SIGKILL);
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

constexpr const char* kLocaleArchive = "/usr/lib/locale/locale-archive";

std::set<std::string> locale_dir_names() {
  std::set<std::string> names;
  if (DIR* directory = opendir("/usr/lib/locale")) {
    while (const dirent* item = readdir(directory)) {
      const std::string name = item->d_name;
      if (name != "." && name != "..") {
        names.insert(name);
      }
    }
    closedir(directory);
  }
  return names;
}

void remove_new_locale_entries(const std::set<std::string>& before) {
  for (const auto& name : locale_dir_names()) {
    if (before.count(name) != 0 || name.find('/') != std::string::npos) {
      continue;
    }
    const std::string path = std::string("/usr/lib/locale/") + name;
    run_command({"rm", "-rf", "--", path}, "");
  }
}

std::string locale_dir_text(const std::set<std::string>& names) {
  std::string text;
  for (const auto& name : names) {
    text += name;
    text += '\n';
  }
  return text;
}

std::set<std::string> locale_dir_from_text(const std::string& text) {
  std::set<std::string> names;
  std::string line;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      if (!line.empty()) {
        names.insert(line);
      }
      line.clear();
    } else {
      line.push_back(text[i]);
    }
  }
  return names;
}

// Debian locale-gen deletes locale-archive and then compiles every enabled
// locale. Stopping that leaves a partial archive and can orphan localedef.
// Compile only the new locale into the existing archive instead.
std::string localedef_input(const std::string& locale) {
  const std::string user_root = "/usr/local/share/i18n/locales/";
  const std::string system_root = "/usr/share/i18n/locales/";
  if (access((user_root + locale).c_str(), F_OK) == 0) {
    return user_root + locale;
  }
  if (access((system_root + locale).c_str(), F_OK) == 0) {
    return locale;
  }
  std::string base = locale;
  const auto at = base.find('@');
  std::string modifier;
  if (at != std::string::npos) {
    modifier = base.substr(at);
    base.resize(at);
  }
  const auto dot = base.find('.');
  if (dot != std::string::npos) {
    base.resize(dot);
  }
  const std::string input = base + modifier;
  if (access((user_root + input).c_str(), F_OK) == 0) {
    return user_root + input;
  }
  return input;
}

void drop_undo_file(const std::string& token, const std::string& name) {
  unlink(undo_path(token, name).c_str());
}

void drop_token_dir_if_empty(const std::string& token) {
  const std::string dir = undo_dir(token);
  if (DIR* directory = opendir(dir.c_str())) {
    bool empty = true;
    while (const dirent* item = readdir(directory)) {
      const std::string name = item->d_name;
      if (name != "." && name != "..") {
        empty = false;
        break;
      }
    }
    closedir(directory);
    if (empty) {
      rmdir(dir.c_str());
    }
  }
}

unsigned caller_uid() {
  const char* value = std::getenv("PKEXEC_UID");
  if (value == nullptr || value[0] == '\0') {
    return getuid();
  }
  try {
    return static_cast<unsigned>(std::stoul(value));
  } catch (const std::exception&) {
    fail("invalid caller");
  }
  return 0;
}

const passwd* account_or_fail(const std::string& name) {
  if (!lcos::valid_username(name)) {
    fail("invalid username");
  }
  const passwd* entry = getpwnam(name.c_str());
  if (entry == nullptr || entry->pw_shell == nullptr ||
      !lcos::is_local_human_account(entry->pw_uid, entry->pw_shell)) {
    fail("that is not a local user account");
  }
  return entry;
}

bool in_sudo(const std::string& name) {
  const group* sudo = getgrnam("sudo");
  if (sudo == nullptr) {
    return false;
  }
  const gid_t sudo_gid = sudo->gr_gid;
  std::vector<std::string> members;
  for (char** member = sudo->gr_mem; member != nullptr && *member != nullptr; ++member) {
    members.emplace_back(*member);
  }
  const passwd* entry = getpwnam(name.c_str());
  if (entry != nullptr && entry->pw_gid == sudo_gid) {
    return true;
  }
  for (const auto& member : members) {
    if (member == name) {
      return true;
    }
  }
  return false;
}

int administrator_count() {
  const group* sudo = getgrnam("sudo");
  if (sudo == nullptr) {
    return 0;
  }
  const gid_t sudo_gid = sudo->gr_gid;
  std::vector<std::string> members;
  for (char** member = sudo->gr_mem; member != nullptr && *member != nullptr; ++member) {
    members.emplace_back(*member);
  }
  std::set<std::string> names;
  for (const auto& member : members) {
    const passwd* entry = getpwnam(member.c_str());
    if (entry != nullptr && entry->pw_name != nullptr && entry->pw_shell != nullptr &&
        lcos::is_local_human_account(entry->pw_uid, entry->pw_shell)) {
      names.insert(entry->pw_name);
    }
  }
  setpwent();
  while (const passwd* entry = getpwent()) {
    if (entry->pw_name != nullptr && entry->pw_shell != nullptr && entry->pw_gid == sudo_gid &&
        lcos::is_local_human_account(entry->pw_uid, entry->pw_shell)) {
      names.insert(entry->pw_name);
    }
  }
  endpwent();
  return static_cast<int>(names.size());
}

void refuse_last_admin(const std::string& name) {
  if (in_sudo(name) && administrator_count() <= 1) {
    fail("the last administrator cannot be removed or demoted");
  }
}

void refuse_self(const passwd* entry) {
  if (entry->pw_uid == caller_uid()) {
    fail("the current user cannot be deleted");
  }
}

bool chrony_installed() { return access("/usr/sbin/chronyd", X_OK) == 0 || access("/usr/bin/chronyc", X_OK) == 0; }

bool chrony_enabled() { return lcos::chrony_is_enabled(); }

bool password_matches(const std::string& name, const std::string& password) {
  const spwd* shadow = getspnam(name.c_str());
  if (shadow == nullptr || shadow->sp_pwdp == nullptr) {
    return false;
  }
  const char* hashed = crypt(password.c_str(), shadow->sp_pwdp);
  if (hashed == nullptr) {
    return false;
  }
  return std::strcmp(hashed, shadow->sp_pwdp) == 0;
}

void set_password_hash(const std::string& name, const std::string& password) {
  const std::string payload = name + ":" + password + "\n";
  if (!run_command({"chpasswd"}, payload)) {
    fail("could not set the password");
  }
}

std::string zoneinfo_target(const std::string& name) {
  if (!lcos::valid_timezone_name(name)) {
    fail("invalid time zone");
  }
  const std::string path = "/usr/share/zoneinfo/" + name;
  char resolved[4096];
  if (realpath(path.c_str(), resolved) == nullptr) {
    fail("unknown time zone");
  }
  const std::string full(resolved);
  const std::string prefix = "/usr/share/zoneinfo/";
  if (full.compare(0, prefix.size(), prefix) != 0) {
    fail("invalid time zone");
  }
  struct stat st;
  if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
    fail("unknown time zone");
  }
  return name;
}

void action_timezone(const std::string& token, const std::string& name) {
  const std::string zone = zoneinfo_target(name);
  char current[4096];
  const ssize_t n = readlink("/etc/localtime", current, sizeof(current) - 1);
  if (n > 0) {
    current[n] = '\0';
    remember_text(token, "localtime", current);
  }
  remember_file(token, "timezone", "/etc/timezone");
  write_file("/etc/timezone", zone + "\n", 0644);
  unlink("/etc/localtime");
  if (symlink(("/usr/share/zoneinfo/" + zone).c_str(), "/etc/localtime") != 0) {
    fail("could not set the time zone");
  }
}

void action_time(const std::string& token, const std::string& date, const std::string& clock) {
  if (date.size() != 10 || clock.size() != 8 || date[4] != '-' || date[7] != '-' || clock[2] != ':' ||
      clock[5] != ':') {
    fail("invalid date or time");
  }
  for (std::size_t i = 0; i < date.size(); ++i) {
    if (i == 4 || i == 7) {
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(date[i]))) {
      fail("invalid date or time");
    }
  }
  for (std::size_t i = 0; i < clock.size(); ++i) {
    if (i == 2 || i == 5) {
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(clock[i]))) {
      fail("invalid date or time");
    }
  }
  if (chrony_enabled()) {
    fail("turn off automatic time before setting the clock");
  }
  struct timespec mono {};
  if (clock_gettime(CLOCK_MONOTONIC, &mono) != 0) {
    fail("could not read the clock");
  }
  // Sample the wall clock and a monotonic clock together, before date -s.
  // Revert adds the elapsed monotonic time back, so Cancel does not wind
  // the clock back by the time spent on the page.
  remember_text(token, "clock-wall", std::to_string(std::time(nullptr)));
  remember_text(token, "clock-mono", std::to_string(mono.tv_sec));
  if (!run_command({"date", "-s", date + " " + clock}, "")) {
    fail("could not set the clock");
  }
}

void action_ntp(const std::string& token, const std::string& state) {
  if (!chrony_installed()) {
    fail("chrony is not installed");
  }
  if (state != "on" && state != "off") {
    fail("invalid automatic time setting");
  }
  const bool was_on = chrony_enabled();
  remember_text(token, "ntp", was_on ? "on" : "off");
  if (state == "on") {
    run_command({"update-rc.d", "chrony", "enable"}, "");
    if (!run_command({"/etc/init.d/chrony", "start"}, "")) {
      if (!was_on) {
        run_command({"update-rc.d", "chrony", "disable"}, "");
      }
      fail("could not start chrony");
    }
  } else {
    run_command({"/etc/init.d/chrony", "stop"}, "");
    if (!run_command({"update-rc.d", "chrony", "disable"}, "")) {
      if (was_on) {
        run_command({"update-rc.d", "chrony", "enable"}, "");
        run_command({"/etc/init.d/chrony", "start"}, "");
      }
      fail("could not disable chrony");
    }
  }
}

std::string capture_command(const std::vector<std::string>& args) {
  if (args.empty()) {
    return {};
  }
  int pipes[2] = {-1, -1};
  if (pipe(pipes) != 0) {
    return {};
  }
  const pid_t pid = fork();
  if (pid < 0) {
    close(pipes[0]);
    close(pipes[1]);
    return {};
  }
  if (pid == 0) {
    dup2(pipes[1], STDOUT_FILENO);
    close(pipes[0]);
    close(pipes[1]);
    std::vector<char*> argv;
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  close(pipes[1]);
  std::string output;
  char buffer[512];
  while (true) {
    const ssize_t n = read(pipes[0], buffer, sizeof(buffer));
    if (n <= 0) {
      break;
    }
    output.append(buffer, buffer + n);
  }
  close(pipes[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  return output;
}

bool builtin_locale(const std::string& name) {
  return name == "C" || name == "C.UTF-8" || name == "POSIX";
}

void action_locale(const std::string& token, const std::vector<std::string>& locales) {
  if (locales.size() != 4) {
    fail("invalid locale request");
  }
  const auto supported = lcos::parse_supported_locales(slurp("/usr/share/i18n/SUPPORTED"));
  if (supported.empty()) {
    fail("the supported locale list is not available");
  }
  const auto generated = lcos::generated_locale_names(capture_command({"locale", "-a"}));
  const std::string previous_gen = slurp("/etc/locale.gen");
  std::string locale_gen = previous_gen;
  bool need_generate = false;
  for (const auto& name : locales) {
    if (name.empty() || builtin_locale(name)) {
      continue;
    }
    if (!lcos::valid_locale_name(name)) {
      fail("invalid locale");
    }
    const lcos::SupportedLocale* locale = lcos::find_supported_locale(supported, name);
    if (locale == nullptr) {
      fail("that locale is not in the supported list");
    }
    if (std::find(generated.begin(), generated.end(), name) != generated.end()) {
      continue;
    }
    std::string updated;
    if (!lcos::enable_locale_in_gen(locale_gen, *locale, supported, updated)) {
      fail("that locale is not in the supported list");
    }
    locale_gen = std::move(updated);
    need_generate = true;
  }
  if (need_generate) {
    const bool created_gen_undo = !undo_exists(token, "locale.gen");
    const bool created_archive_undo = !undo_exists(token, "locale.archive") &&
                                      !undo_exists(token, "locale.archive.missing");
    const bool created_dir_undo = !undo_exists(token, "locale.dir");
    remember_file(token, "locale.gen", "/etc/locale.gen");
    const bool archive_existed = access(kLocaleArchive, F_OK) == 0;
    const std::string previous_archive = archive_existed ? slurp(kLocaleArchive) : std::string();
    remember_file(token, "locale.archive", kLocaleArchive);
    const std::set<std::string> locale_files_before = locale_dir_names();
    remember_text(token, "locale.dir", locale_dir_text(locale_files_before));
    write_file("/etc/locale.gen", locale_gen, 0644);
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = on_locale_term;
    sigaction(SIGTERM, &action, nullptr);
    arm_locale_cancel_pipe();
    bool compiled = true;
    std::vector<const lcos::SupportedLocale*> pending;
    for (const auto& name : locales) {
      if (name.empty() || builtin_locale(name)) {
        continue;
      }
      if (std::find(generated.begin(), generated.end(), name) != generated.end()) {
        continue;
      }
      const lcos::SupportedLocale* locale = lcos::find_supported_locale(supported, name);
      if (locale != nullptr) {
        pending.push_back(locale);
      }
    }
    const char* alias_file = "/usr/share/locale/locale.alias";
    const bool have_alias = access(alias_file, R_OK) == 0;
    for (const lcos::SupportedLocale* locale : pending) {
      if (g_cancel_locale != 0) {
        compiled = false;
        break;
      }
      std::vector<std::string> command = {"/usr/bin/localedef", "-i", localedef_input(locale->name),
                                          "-c", "-f", locale->charset};
      if (have_alias) {
        command.push_back("-A");
        command.push_back(alias_file);
      }
      command.push_back(locale->name);
      if (!run_cancellable(command)) {
        compiled = false;
        break;
      }
    }
    action.sa_handler = SIG_DFL;
    sigaction(SIGTERM, &action, nullptr);
    if (!compiled || g_cancel_locale != 0) {
      write_file("/etc/locale.gen", previous_gen, 0644);
      if (archive_existed) {
        write_file(kLocaleArchive, previous_archive, 0644);
      }
      remove_new_locale_entries(locale_files_before);
      // This run's undo files are the pre-generation bytes, which are now
      // back on disk. Drop them so a cancelled run does not leave
      // /run/lcos-settings/<token> behind. An older snapshot from a change
      // that already succeeded in this visit stays for window Cancel.
      if (created_gen_undo) {
        drop_undo_file(token, "locale.gen");
        drop_undo_file(token, "locale.gen.missing");
      }
      if (created_archive_undo) {
        drop_undo_file(token, "locale.archive");
        drop_undo_file(token, "locale.archive.missing");
      }
      if (created_dir_undo) {
        drop_undo_file(token, "locale.dir");
      }
      drop_token_dir_if_empty(token);
      fail(g_cancel_locale != 0 ? "locale generation was cancelled" : "could not generate the locale");
    }
  }
  remember_file(token, "locale", "/etc/default/locale");
  const std::string text = lcos::locale_file_with_values(slurp("/etc/default/locale"), locales[0], locales[1],
                                                        locales[2], locales[3]);
  write_file("/etc/default/locale", text, 0644);
}

std::string shadow_field(long value) { return value < 0 ? std::string() : std::to_string(value); }

std::string format_shadow(const spwd* shadow) {
  const std::string flag =
      shadow->sp_flag == 0 ? std::string() : std::to_string(static_cast<unsigned long>(shadow->sp_flag));
  std::ostringstream out;
  out << (shadow->sp_namp != nullptr ? shadow->sp_namp : "") << ':'
      << (shadow->sp_pwdp != nullptr ? shadow->sp_pwdp : "") << ':' << shadow_field(shadow->sp_lstchg)
      << ':' << shadow_field(shadow->sp_min) << ':' << shadow_field(shadow->sp_max) << ':'
      << shadow_field(shadow->sp_warn) << ':' << shadow_field(shadow->sp_inact) << ':'
      << shadow_field(shadow->sp_expire) << ':' << flag;
  return out.str();
}

void remember_account(const std::string& token, const std::string& name) {
  const passwd* entry = getpwnam(name.c_str());
  if (entry == nullptr || entry->pw_name == nullptr) {
    return;
  }
  const uid_t uid = entry->pw_uid;
  const gid_t gid = entry->pw_gid;
  const std::string user = entry->pw_name;
  const std::string gecos = entry->pw_gecos != nullptr ? entry->pw_gecos : "";
  const std::string home = entry->pw_dir != nullptr ? entry->pw_dir : "";
  const std::string shell = entry->pw_shell != nullptr ? entry->pw_shell : "";
  const std::string passwd_field = entry->pw_passwd != nullptr ? entry->pw_passwd : "x";
  std::ostringstream passwd_line;
  passwd_line << user << ":" << passwd_field << ":" << uid << ":" << gid << ":" << gecos << ":"
              << home << ":" << shell << "\n";
  remember_text(token, "passwd." + user, passwd_line.str());
  const spwd* shadow = getspnam(user.c_str());
  if (shadow == nullptr || shadow->sp_namp == nullptr) {
    fail("could not read the password entry");
  }
  remember_text(token, "shadow." + user, format_shadow(shadow) + "\n");
  std::string primary_name;
  if (const group* primary = getgrgid(gid)) {
    if (primary->gr_name != nullptr) {
      primary_name = primary->gr_name;
    }
  }
  if (primary_name.empty()) {
    primary_name = user;
  }
  remember_text(token, "group." + user, primary_name + "\n" + std::to_string(gid) + "\n");
  int slots = 0;
  getgrouplist(user.c_str(), gid, nullptr, &slots);
  std::vector<gid_t> groups(static_cast<std::size_t>(std::max(slots, 1)));
  const int count = slots;
  std::string supplementary;
  if (getgrouplist(user.c_str(), gid, groups.data(), &slots) >= 0) {
    for (int i = 0; i < count && i < slots; ++i) {
      if (groups[static_cast<std::size_t>(i)] == gid) {
        continue;
      }
      const group* extra = getgrgid(groups[static_cast<std::size_t>(i)]);
      if (extra == nullptr || extra->gr_name == nullptr || extra->gr_name[0] == '\0') {
        continue;
      }
      if (!supplementary.empty()) {
        supplementary += ",";
      }
      supplementary += extra->gr_name;
    }
  }
  remember_text(token, "groups." + user, supplementary);
  remember_text(token, "sudo." + name, in_sudo(name) ? "yes" : "no");
}

void action_add_user(const std::string& token, const std::string& name, const std::string& real,
                     const std::string& kind, const std::string& password) {
  if (!lcos::valid_username(name) || !lcos::valid_real_name(real) || !lcos::valid_password(password)) {
    fail("invalid new account");
  }
  if (kind != "standard" && kind != "administrator") {
    fail("invalid account type");
  }
  if (getpwnam(name.c_str()) != nullptr) {
    fail("that username already exists");
  }
  if (!run_command({"useradd", "-m", "-s", "/bin/bash", "-c", real, name}, "")) {
    fail("could not create the account");
  }
  remember_text(token, "added." + name, "1");
  if (!run_command({"chpasswd"}, name + ":" + password + "\n")) {
    run_command({"userdel", "-r", name}, "");
    fail("could not set the password");
  }
  if (kind == "administrator" && !run_command({"usermod", "-a", "-G", "sudo", name}, "")) {
    run_command({"userdel", "-r", name}, "");
    fail("could not create the account");
  }
}

void action_remove_user(const std::string& token, const std::string& name) {
  const passwd* entry = account_or_fail(name);
  refuse_self(entry);
  refuse_last_admin(name);
  remember_account(token, name);
  // Leave the home directory where it is. userdel without -r does not delete
  // it, and Cancel recreates the account on that same path. Moving it into
  // /run would put the only copy on tmpfs.
  if (!run_command({"userdel", name}, "")) {
    fail("could not remove the account");
  }
}

void action_password(const std::string& token, const std::string& name, const std::string& mode,
                     const std::string& input) {
  const passwd* entry = account_or_fail(name);
  const std::string password = mode == "self" ? second_line(input) : first_line(input);
  if (!lcos::valid_password(password)) {
    fail("invalid password");
  }
  if (mode == "self") {
    if (entry->pw_uid != caller_uid()) {
      fail("enter your own current password to change it");
    }
    if (!password_matches(name, first_line(input))) {
      fail("the current password is not correct");
    }
  } else if (mode != "other") {
    fail("invalid password request");
  }
  remember_account(token, name);
  set_password_hash(name, password);
}

void action_type(const std::string& token, const std::string& name, const std::string& kind) {
  account_or_fail(name);
  if (kind != "standard" && kind != "administrator") {
    fail("invalid account type");
  }
  if (kind == "standard") {
    refuse_last_admin(name);
  }
  remember_account(token, name);
  if (kind == "administrator") {
    if (!run_command({"usermod", "-a", "-G", "sudo", name}, "")) {
      fail("could not change the account type");
    }
  } else if (!run_command({"gpasswd", "-d", name, "sudo"}, "")) {
    fail("could not change the account type");
  }
}

void action_real_name(const std::string& token, const std::string& name, const std::string& real) {
  account_or_fail(name);
  if (!lcos::valid_real_name(real)) {
    fail("invalid name");
  }
  remember_account(token, name);
  if (!run_command({"chfn", "-f", real, name}, "")) {
    fail("could not change the name");
  }
}

bool looks_like_image(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  unsigned char magic[8] = {};
  in.read(reinterpret_cast<char*>(magic), 8);
  if (!in && in.gcount() < 8) {
    return false;
  }
  const bool png = magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G';
  const bool jpeg = magic[0] == 0xff && magic[1] == 0xd8 && magic[2] == 0xff;
  return png || jpeg;
}

void action_avatar(const std::string& token, const std::string& name, const std::string& source) {
  const passwd* entry = account_or_fail(name);
  if (source.empty() || source[0] != '/' || source.find("..") != std::string::npos) {
    fail("invalid picture");
  }
  char resolved[4096];
  if (realpath(source.c_str(), resolved) == nullptr) {
    fail("invalid picture");
  }
  struct stat st;
  if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 2 * 1024 * 1024) {
    fail("invalid picture");
  }
  if (!looks_like_image(resolved)) {
    fail("the picture must be a PNG or JPEG file");
  }
  const std::string dest = std::string(entry->pw_dir) + "/.face";
  struct stat face_st;
  if (lstat(dest.c_str(), &face_st) == 0 && !S_ISREG(face_st.st_mode)) {
    fail("refusing to replace a non-regular picture file");
  }
  remember_file(token, "avatar." + name, dest);
  std::ifstream in(resolved, std::ios::binary);
  std::ostringstream data;
  data << in.rdbuf();
  lcos::ReplaceRequest request;
  request.mode_if_new = 0644;
  request.force_owner = true;
  request.uid = entry->pw_uid;
  request.gid = entry->pw_gid;
  std::string error;
  if (!lcos::replace_regular_file(dest, data.str(), request, &error)) {
    fail(error.empty() ? "cannot write the picture" : error);
  }
}

void action_autologin(const std::string& token, const std::string& user) {
  if (!(user == "none" || lcos::valid_username(user))) {
    fail("invalid automatic login");
  }
  if (user != "none") {
    account_or_fail(user);
  }
  // 50-lcos.conf is never edited. The drop-in wins on systems where the main
  // file does not set autologin-user. LightDM reads /etc/lightdm/lightdm.conf
  // after every drop-in, so a key set there has to be changed in that file.
  const char* dropin = "/etc/lightdm/lightdm.conf.d/60-lcos-settings.conf";
  const char* main_conf = "/etc/lightdm/lightdm.conf";
  remember_file(token, "lightdm.0", dropin);
  mkdir("/etc/lightdm", 0755);
  mkdir("/etc/lightdm/lightdm.conf.d", 0755);
  const std::string value = user == "none" ? std::string() : user;
  const std::string text = std::string("[Seat:*]\n") +
                           "# LCOS Command Center. The installer file is left unchanged.\n" +
                           "autologin-user=" + value + "\n" + "autologin-user-timeout=0\n";
  write_file(dropin, text, 0644);
  const std::string main = slurp(main_conf);
  std::string existing;
  if (lcos::autologin_key_present(main, existing)) {
    remember_file(token, "lightdm.1", main_conf);
    write_file(main_conf, lcos::conf_with_autologin(main, value), 0644);
  }
}

bool try_write_file(const std::string& path, const std::string& text, mode_t mode,
                     std::string& error) {
  if (path == "/etc/lightdm/lightdm.conf.d/50-lcos.conf") {
    error = "refusing to edit the installer LightDM file";
    return false;
  }
  lcos::ReplaceRequest request;
  request.mode_if_new = mode;
  if (path == "/etc/default/locale") {
    request.follow_symlink = true;
    request.symlink_targets = {"/etc/locale.conf", "/etc/default/locale"};
  }
  if (!lcos::replace_regular_file(path, text, request, &error)) {
    if (error.empty()) {
      error = "cannot write " + path;
    }
    return false;
  }
  return true;
}

bool restore_shadow_password(const std::string& saved_line, std::string& error) {
  // Hold the shadow lock across the rewrite. try_write_file copies the current
  // inode's owner, group, and mode, so root:shadow 0640 survives.
  if (lckpwdf() != 0) {
    error = "could not lock the password file";
    return false;
  }
  const auto cut = saved_line.find(':');
  const std::string name = cut == std::string::npos ? std::string() : saved_line.substr(0, cut);
  const bool exists = !name.empty() && getpwnam(name.c_str()) != nullptr;
  const lcos::ShadowRestore restored = lcos::restore_shadow_line(slurp("/etc/shadow"), saved_line, exists);
  if (!restored.ok) {
    ulckpwdf();
    error = restored.error.empty() ? "could not restore the password entry" : restored.error;
    return false;
  }
  std::string write_error;
  const bool wrote = try_write_file("/etc/shadow", restored.text, 0640, write_error);
  ulckpwdf();
  if (!wrote) {
    error = write_error.empty() ? "could not restore the password entry" : write_error;
  }
  return wrote;
}

struct RevertReport {
  std::vector<std::string> errors;
  void check(bool ok, const std::string& message) {
    if (!ok && !message.empty()) {
      errors.push_back(message);
    }
  }
};

bool ensure_saved_group(const std::string& name, const std::string& gid_text, std::string& error) {
  if (name.empty() || gid_text.empty()) {
    error = "the saved group is incomplete";
    return false;
  }
  char* end = nullptr;
  const unsigned long gid = std::strtoul(gid_text.c_str(), &end, 10);
  if (end == gid_text.c_str() || *end != '\0') {
    error = "the saved group is incomplete";
    return false;
  }
  if (getgrgid(static_cast<gid_t>(gid)) != nullptr) {
    return true;
  }
  if (getgrnam(name.c_str()) != nullptr) {
    error = "could not restore the private group";
    return false;
  }
  if (!run_command({"groupadd", "-o", "-g", gid_text, name}, "")) {
    error = "could not restore the private group";
    return false;
  }
  return true;
}

std::vector<std::string> split_commas(const std::string& text) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : text) {
    if (c == ',') {
      if (!current.empty()) {
        parts.push_back(current);
      }
      current.clear();
    } else if (c != ' ' && c != '\n' && c != '\r') {
      current.push_back(c);
    }
  }
  if (!current.empty()) {
    parts.push_back(current);
  }
  return parts;
}

std::vector<std::string> undo_names(const std::string& dir) {
  std::vector<std::string> names;
  if (DIR* directory = opendir(dir.c_str())) {
    while (const dirent* item = readdir(directory)) {
      const std::string name = item->d_name;
      if (name != "." && name != "..") {
        names.push_back(name);
      }
    }
    closedir(directory);
  }
  return names;
}

void discard_domain_files(const std::string& token, lcos::PrivDomain domain) {
  const std::string dir = undo_dir(token);
  for (const auto& name : undo_names(dir)) {
    if (lcos::undo_file_belongs_to(domain, name)) {
      unlink((dir + "/" + name).c_str());
    }
  }
  drop_token_dir_if_empty(token);
}

bool revert_token(const std::string& token, lcos::PrivDomain domain) {
  ensure_undo(token);
  const std::string dir = undo_dir(token);
  RevertReport report;
  auto note_command = [&](bool ok, const std::string& message) { report.check(ok, message); };

  if (domain == lcos::PrivDomain::System) {
    if (access((dir + "/localtime").c_str(), F_OK) == 0) {
      const std::string target = first_line(slurp(dir + "/localtime"));
      if (unlink("/etc/localtime") != 0 && errno != ENOENT) {
        report.errors.push_back("could not restore the time zone link");
      } else if (target.empty() || symlink(target.c_str(), "/etc/localtime") != 0) {
        report.errors.push_back("could not restore the time zone link");
      }
    }
    if (access((dir + "/timezone").c_str(), F_OK) == 0) {
      std::string error;
      bool ok = true;
      if (access((dir + "/timezone.missing").c_str(), F_OK) == 0) {
        if (unlink("/etc/timezone") != 0 && errno != ENOENT) {
          ok = false;
          error = "could not restore the time zone name";
        }
      } else {
        ok = try_write_file("/etc/timezone", slurp(dir + "/timezone"), 0644, error);
      }
      report.check(ok, error.empty() ? "could not restore the time zone name" : error);
    }
    if (access((dir + "/ntp").c_str(), F_OK) == 0) {
      const std::string state = first_line(slurp(dir + "/ntp"));
      if (state == "on") {
        note_command(run_command({"update-rc.d", "chrony", "enable"}, ""), "could not restore automatic time");
        note_command(run_command({"/etc/init.d/chrony", "start"}, ""), "could not restore automatic time");
      } else if (state == "off") {
        note_command(run_command({"/etc/init.d/chrony", "stop"}, ""), "could not restore automatic time");
        note_command(run_command({"update-rc.d", "chrony", "disable"}, ""), "could not restore automatic time");
      }
    }
    if (access((dir + "/clock-wall").c_str(), F_OK) == 0 &&
        access((dir + "/clock-mono").c_str(), F_OK) == 0) {
      try {
        const std::time_t saved_wall = static_cast<std::time_t>(std::stoll(first_line(slurp(dir + "/clock-wall"))));
        const std::time_t saved_mono = static_cast<std::time_t>(std::stoll(first_line(slurp(dir + "/clock-mono"))));
        struct timespec now {};
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
          report.errors.push_back("could not restore the clock");
        } else {
          const std::time_t target = lcos::restored_wall_clock(saved_wall, saved_mono, now.tv_sec);
          note_command(run_command({"date", "-s", "@" + std::to_string(target)}, ""),
                       "could not restore the clock");
        }
      } catch (const std::exception&) {
        report.errors.push_back("could not restore the clock");
      }
    }
  }

  if (domain == lcos::PrivDomain::Locale) {
    if (access((dir + "/locale.gen").c_str(), F_OK) == 0) {
      std::string error;
      bool ok = true;
      if (access((dir + "/locale.gen.missing").c_str(), F_OK) == 0) {
        if (unlink("/etc/locale.gen") != 0 && errno != ENOENT) {
          ok = false;
        }
      } else {
        ok = try_write_file("/etc/locale.gen", slurp(dir + "/locale.gen"), 0644, error);
      }
      report.check(ok, error.empty() ? "could not restore locale.gen" : error);
    }
    if (access((dir + "/locale.archive").c_str(), F_OK) == 0) {
      std::string error;
      bool ok = true;
      if (access((dir + "/locale.archive.missing").c_str(), F_OK) == 0) {
        if (unlink(kLocaleArchive) != 0 && errno != ENOENT) {
          ok = false;
        }
      } else {
        ok = try_write_file(kLocaleArchive, slurp(dir + "/locale.archive"), 0644, error);
      }
      report.check(ok, error.empty() ? "could not restore the locale archive" : error);
    }
    if (access((dir + "/locale.dir").c_str(), F_OK) == 0) {
      remove_new_locale_entries(locale_dir_from_text(slurp(dir + "/locale.dir")));
    }
    if (access((dir + "/locale").c_str(), F_OK) == 0) {
      std::string error;
      bool ok = true;
      if (access((dir + "/locale.missing").c_str(), F_OK) == 0) {
        if (unlink("/etc/default/locale") != 0 && errno != ENOENT) {
          ok = false;
        }
      } else {
        ok = try_write_file("/etc/default/locale", slurp(dir + "/locale"), 0644, error);
      }
      report.check(ok, error.empty() ? "could not restore the locale" : error);
    }
  }

  if (domain == lcos::PrivDomain::Accounts) {
    for (const auto& name : undo_names(dir)) {
      if (name.compare(0, 6, "added.") != 0) {
        continue;
      }
      const std::string user = name.substr(6);
      note_command(run_command({"userdel", "-r", user}, ""), "could not remove the added account");
    }
    for (const auto& name : undo_names(dir)) {
      if (name.compare(0, 7, "passwd.") != 0) {
        continue;
      }
      const std::string user = name.substr(7);
      if (getpwnam(user.c_str()) != nullptr) {
        continue;
      }
      const std::string line = first_line(slurp(dir + "/" + name));
      std::vector<std::string> fields;
      std::string field;
      for (char c : line) {
        if (c == ':') {
          fields.push_back(field);
          field.clear();
        } else {
          field.push_back(c);
        }
      }
      fields.push_back(field);
      if (fields.size() < 7 || fields[0].empty()) {
        report.errors.push_back("the saved account is incomplete");
        continue;
      }
      std::string group_error;
      const std::string group_text = slurp(dir + "/group." + user);
      const auto nl = group_text.find('\n');
      const std::string group_name = nl == std::string::npos ? group_text : group_text.substr(0, nl);
      const std::string group_gid =
          nl == std::string::npos ? std::string() : first_line(group_text.substr(nl + 1));
      if (!group_name.empty() && !ensure_saved_group(group_name, group_gid.empty() ? fields[3] : group_gid,
                                                    group_error)) {
        report.errors.push_back(group_error);
        continue;
      }
      if (!run_command({"useradd", "-M", "-o", "-u", fields[2], "-g", fields[3], "-d", fields[5], "-s",
                        fields[6], "-c", fields[4], fields[0]},
                       "")) {
        report.errors.push_back("could not restore the account");
        continue;
      }
      endpwent();
      endgrent();
      if (access((dir + "/shadow." + user).c_str(), F_OK) == 0) {
        std::string shadow_error;
        report.check(restore_shadow_password(first_line(slurp(dir + "/shadow." + user)), shadow_error),
                     shadow_error.empty() ? "could not restore the password entry" : shadow_error);
      }
      for (const auto& extra : split_commas(slurp(dir + "/groups." + user))) {
        if (getgrnam(extra.c_str()) == nullptr) {
          report.errors.push_back("could not restore group " + extra);
          continue;
        }
        note_command(run_command({"usermod", "-a", "-G", extra, user}, ""),
                     "could not restore group " + extra);
      }
      if (first_line(slurp(dir + "/sudo." + user)) == "yes" && !in_sudo(user)) {
        note_command(run_command({"usermod", "-a", "-G", "sudo", user}, ""),
                     "could not restore administrator access");
      }
    }
    for (const auto& name : undo_names(dir)) {
      if (name.compare(0, 7, "shadow.") == 0 && name.find(".missing") == std::string::npos &&
          getpwnam(name.substr(7).c_str()) != nullptr) {
        std::string shadow_error;
        report.check(restore_shadow_password(first_line(slurp(dir + "/" + name)), shadow_error),
                     shadow_error.empty() ? "could not restore the password entry" : shadow_error);
      } else if (name.compare(0, 5, "sudo.") == 0) {
        const std::string user = name.substr(5);
        if (getpwnam(user.c_str()) == nullptr) {
          continue;
        }
        const bool want = first_line(slurp(dir + "/" + name)) == "yes";
        if (want && !in_sudo(user)) {
          note_command(run_command({"usermod", "-a", "-G", "sudo", user}, ""),
                       "could not restore administrator access");
        } else if (!want && in_sudo(user)) {
          note_command(run_command({"gpasswd", "-d", user, "sudo"}, ""),
                       "could not restore the account type");
        }
      } else if (name.compare(0, 7, "avatar.") == 0 && name.find(".missing") == std::string::npos) {
        const std::string user = name.substr(7);
        const passwd* entry = getpwnam(user.c_str());
        if (entry == nullptr || entry->pw_dir == nullptr) {
          report.errors.push_back("could not restore the picture");
          continue;
        }
        const std::string dest = std::string(entry->pw_dir) + "/.face";
        if (access((dir + "/" + name + ".missing").c_str(), F_OK) == 0) {
          if (unlink(dest.c_str()) != 0 && errno != ENOENT) {
            report.errors.push_back("could not restore the picture");
          }
        } else {
          struct stat face_st;
          if (lstat(dest.c_str(), &face_st) == 0 && !S_ISREG(face_st.st_mode)) {
            if (unlink(dest.c_str()) != 0) {
              report.errors.push_back("could not restore the picture");
              continue;
            }
          }
          lcos::ReplaceRequest request;
          request.mode_if_new = 0644;
          request.force_owner = true;
          request.uid = entry->pw_uid;
          request.gid = entry->pw_gid;
          std::string error;
          report.check(lcos::replace_regular_file(dest, slurp(dir + "/" + name), request, &error),
                       error.empty() ? "could not restore the picture" : error);
        }
      } else if (name.compare(0, 8, "lightdm.") == 0 && name.find(".missing") == std::string::npos) {
        const char* paths[] = {"/etc/lightdm/lightdm.conf.d/60-lcos-settings.conf",
                               "/etc/lightdm/lightdm.conf"};
        const int index = std::atoi(name.c_str() + 8);
        if (index >= 0 && index < 2) {
          std::string error;
          bool ok = true;
          if (access((dir + "/" + name + ".missing").c_str(), F_OK) == 0) {
            if (unlink(paths[index]) != 0 && errno != ENOENT) {
              ok = false;
              error = "could not restore automatic login";
            }
          } else {
            ok = try_write_file(paths[index], slurp(dir + "/" + name), 0644, error);
          }
          report.check(ok, error.empty() ? "could not restore automatic login" : error);
        }
      }
    }
  }

  if (!report.errors.empty()) {
    std::cerr << "could not restore every change\n";
    for (const auto& message : report.errors) {
      std::cerr << message << "\n";
    }
    return false;
  }
  discard_domain_files(token, domain);
  return true;
}

bool commit_token(const std::string& token, lcos::PrivDomain domain) {
  ensure_undo(token);
  discard_domain_files(token, domain);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (geteuid() != 0) {
    fail("this program must run as root");
  }
  if (argc < 4 || std::strncmp(argv[1], "--domain=", 9) != 0) {
    fail("invalid request");
  }
  const std::string domain_name = argv[1] + 9;
  lcos::PrivDomain domain = lcos::PrivDomain::System;
  if (domain_name == "system") {
    domain = lcos::PrivDomain::System;
  } else if (domain_name == "accounts") {
    domain = lcos::PrivDomain::Accounts;
  } else if (domain_name == "locale") {
    domain = lcos::PrivDomain::Locale;
  } else {
    fail("invalid request");
  }
  const std::string token = argv[2];
  const std::string action = argv[3];
  if (!lcos::valid_undo_token(token)) {
    fail("invalid request");
  }
  if (!lcos::priv_action_allowed(domain, action)) {
    fail("this action is not allowed");
  }
  const int rest = argc - 3;
  try {
    if (action == "revert" && rest == 1) {
      if (!revert_token(token, domain)) {
        return 1;
      }
    } else if (action == "commit" && rest == 1) {
      commit_token(token, domain);
    } else if (action == "set-timezone" && rest == 2) {
      action_timezone(token, argv[4]);
    } else if (action == "set-time" && rest == 3) {
      action_time(token, argv[4], argv[5]);
    } else if (action == "set-ntp" && rest == 2) {
      action_ntp(token, argv[4]);
    } else if (action == "set-locale" && rest == 5) {
      action_locale(token, {argv[4], argv[5], argv[6], argv[7]});
    } else if (action == "add-user" && rest == 4) {
      action_add_user(token, argv[4], argv[5], argv[6], first_line(read_stdin()));
    } else if (action == "remove-user" && rest == 2) {
      action_remove_user(token, argv[4]);
    } else if (action == "set-password" && rest == 3) {
      action_password(token, argv[4], argv[5], read_stdin());
    } else if (action == "set-type" && rest == 3) {
      action_type(token, argv[4], argv[5]);
    } else if (action == "set-real-name" && rest == 3) {
      action_real_name(token, argv[4], argv[5]);
    } else if (action == "set-avatar" && rest == 3) {
      action_avatar(token, argv[4], argv[5]);
    } else if (action == "set-autologin" && rest == 2) {
      action_autologin(token, argv[4]);
    } else {
      fail("invalid request");
    }
  } catch (const std::exception&) {
    fail("could not complete the change");
  }
  return 0;
}
