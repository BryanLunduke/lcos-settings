// SPDX-License-Identifier: GPL-3.0-or-later
// Root-only helper for LCOS Command Center. pkexec starts it. Arguments are
// checked before any change. Passwords arrive on stdin and are never logged
// or placed on a command line.

#include "logic/system_model.hpp"

#include <crypt.h>
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <shadow.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
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
  const std::string tmp = path + ".lcos-tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      fail("cannot write " + path);
    }
    out << text;
    if (!out) {
      fail("cannot write " + path);
    }
  }
  chmod(tmp.c_str(), mode);
  if (rename(tmp.c_str(), path.c_str()) != 0) {
    fail("cannot write " + path);
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
  if (entry == nullptr || entry->pw_uid < 1000 || entry->pw_uid >= 65534) {
    fail("that is not a local user account");
  }
  return entry;
}

bool in_sudo(const std::string& name) {
  const group* sudo = getgrnam("sudo");
  if (sudo == nullptr) {
    return false;
  }
  for (char** member = sudo->gr_mem; member != nullptr && *member != nullptr; ++member) {
    if (name == *member) {
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
  int count = 0;
  for (char** member = sudo->gr_mem; member != nullptr && *member != nullptr; ++member) {
    const passwd* entry = getpwnam(*member);
    if (entry != nullptr && entry->pw_uid >= 1000 && entry->pw_uid < 65534) {
      ++count;
    }
  }
  return count;
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

bool chrony_enabled() {
  DIR* directory = opendir("/etc/rc2.d");
  if (directory == nullptr) {
    return false;
  }
  bool enabled = false;
  while (const dirent* item = readdir(directory)) {
    const std::string name = item->d_name;
    if (!name.empty() && name[0] == 'S' && name.size() > 6 &&
        name.compare(name.size() - 6, 6, "chrony") == 0) {
      enabled = true;
      break;
    }
  }
  closedir(directory);
  return enabled;
}

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

void action_time(const std::string& date, const std::string& clock) {
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
  remember_text(token, "ntp", chrony_enabled() ? "on" : "off");
  if (state == "on") {
    run_command({"update-rc.d", "chrony", "enable"}, "");
    if (!run_command({"/etc/init.d/chrony", "start"}, "")) {
      fail("could not start chrony");
    }
  } else {
    run_command({"/etc/init.d/chrony", "stop"}, "");
    if (!run_command({"update-rc.d", "chrony", "disable"}, "")) {
      fail("could not disable chrony");
    }
  }
}

void action_locale(const std::string& token, const std::vector<std::string>& locales) {
  if (locales.size() != 4) {
    fail("invalid locale request");
  }
  for (const auto& name : locales) {
    if (!lcos::valid_locale_name(name)) {
      fail("invalid locale");
    }
  }
  remember_file(token, "locale", "/etc/default/locale");
  std::string text = "LANG=" + locales[0] + "\n";
  const char* keys[] = {"LC_TIME", "LC_NUMERIC", "LC_MONETARY"};
  for (int i = 0; i < 3; ++i) {
    if (!locales[static_cast<std::size_t>(i + 1)].empty() &&
        locales[static_cast<std::size_t>(i + 1)] != locales[0]) {
      text += std::string(keys[i]) + "=" + locales[static_cast<std::size_t>(i + 1)] + "\n";
    }
  }
  write_file("/etc/default/locale", text, 0644);
}

void remember_account(const std::string& token, const std::string& name) {
  const passwd* entry = getpwnam(name.c_str());
  if (entry == nullptr) {
    return;
  }
  std::ostringstream passwd_line;
  passwd_line << entry->pw_name << ":" << entry->pw_passwd << ":" << entry->pw_uid << ":"
              << entry->pw_gid << ":" << entry->pw_gecos << ":" << entry->pw_dir << ":"
              << entry->pw_shell << "\n";
  remember_text(token, "passwd." + name, passwd_line.str());
  const spwd* shadow = getspnam(name.c_str());
  if (shadow != nullptr && shadow->sp_pwdp != nullptr) {
    remember_text(token, "shadow." + name, std::string(shadow->sp_namp) + ":" + shadow->sp_pwdp + "\n");
  }
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
  set_password_hash(name, password);
  if (kind == "administrator") {
    run_command({"usermod", "-a", "-G", "sudo", name}, "");
  }
  remember_text(token, "added." + name, "1");
}

void action_remove_user(const std::string& token, const std::string& name) {
  const passwd* entry = account_or_fail(name);
  refuse_self(entry);
  refuse_last_admin(name);
  remember_account(token, name);
  const std::string home = entry->pw_dir != nullptr ? entry->pw_dir : "";
  if (home == std::string("/home/") + name) {
    const std::string backup = undo_path(token, "home." + name);
    if (rename(home.c_str(), backup.c_str()) == 0) {
      remember_text(token, "homepath." + name, home);
    }
  }
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
  remember_file(token, "avatar." + name, dest);
  std::ifstream in(resolved, std::ios::binary);
  std::ostringstream data;
  data << in.rdbuf();
  write_file(dest, data.str(), 0644);
  chown(dest.c_str(), entry->pw_uid, entry->pw_gid);
}

void rewrite_autologin(const std::string& path, const std::string& user) {
  const std::string text = slurp(path);
  write_file(path, lcos::conf_with_autologin(text, user), 0644);
}

void action_autologin(const std::string& token, const std::string& user) {
  if (!(user == "none" || lcos::valid_username(user))) {
    fail("invalid automatic login");
  }
  if (user != "none" && getpwnam(user.c_str()) == nullptr) {
    fail("that user does not exist");
  }
  const char* paths[] = {"/etc/lightdm/lightdm.conf.d/50-lcos.conf", "/etc/lightdm/lightdm.conf"};
  int index = 0;
  for (const char* path : paths) {
    if (access(path, F_OK) == 0) {
      remember_file(token, "lightdm." + std::to_string(index), path);
    }
    ++index;
  }
  const std::string value = user == "none" ? std::string() : user;
  const char* primary = "/etc/lightdm/lightdm.conf.d/50-lcos.conf";
  if (access(primary, F_OK) != 0) {
    mkdir("/etc/lightdm", 0755);
    mkdir("/etc/lightdm/lightdm.conf.d", 0755);
    write_file(primary, "", 0644);
  }
  rewrite_autologin(primary, value);
  if (user == "none" && access("/etc/lightdm/lightdm.conf", F_OK) == 0) {
    rewrite_autologin("/etc/lightdm/lightdm.conf", "");
  }
}

void restore_shadow_password(const std::string& saved_line) {
  const auto cut = saved_line.find(':');
  if (cut == std::string::npos) {
    return;
  }
  const std::string name = saved_line.substr(0, cut);
  const auto next = saved_line.find(':', cut + 1);
  const std::string hash = saved_line.substr(cut + 1, next == std::string::npos ? std::string::npos : next - cut - 1);
  std::string text = slurp("/etc/shadow");
  std::string out;
  std::string line;
  bool replaced = false;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i != text.size() && text[i] != '\n') {
      line.push_back(text[i]);
      continue;
    }
    if (line.compare(0, name.size() + 1, name + ":") == 0) {
      out += name + ":" + hash + line.substr(line.find(':', name.size() + 1) == std::string::npos
                                                 ? line.size()
                                                 : line.find(':', name.size() + 1));
      replaced = true;
    } else if (!line.empty()) {
      out += line;
    }
    if (i != text.size() || !line.empty()) {
      out += '\n';
    }
    line.clear();
  }
  if (!replaced) {
    out += saved_line;
    if (out.empty() || out.back() != '\n') {
      out += '\n';
    }
  }
  write_file("/etc/shadow", out, 0640);
}

void revert_token(const std::string& token) {
  ensure_undo(token);
  const std::string dir = undo_dir(token);
  if (access((dir + "/localtime").c_str(), F_OK) == 0) {
    const std::string target = first_line(slurp(dir + "/localtime"));
    unlink("/etc/localtime");
    symlink(target.c_str(), "/etc/localtime");
  }
  if (access((dir + "/timezone").c_str(), F_OK) == 0) {
    if (access((dir + "/timezone.missing").c_str(), F_OK) == 0) {
      unlink("/etc/timezone");
    } else {
      write_file("/etc/timezone", slurp(dir + "/timezone"), 0644);
    }
  }
  if (access((dir + "/ntp").c_str(), F_OK) == 0) {
    const std::string state = first_line(slurp(dir + "/ntp"));
    if (state == "on") {
      run_command({"update-rc.d", "chrony", "enable"}, "");
      run_command({"/etc/init.d/chrony", "start"}, "");
    } else if (state == "off") {
      run_command({"/etc/init.d/chrony", "stop"}, "");
      run_command({"update-rc.d", "chrony", "disable"}, "");
    }
  }
  if (access((dir + "/locale").c_str(), F_OK) == 0) {
    if (access((dir + "/locale.missing").c_str(), F_OK) == 0) {
      unlink("/etc/default/locale");
    } else {
      write_file("/etc/default/locale", slurp(dir + "/locale"), 0644);
    }
  }
  // Added accounts go away. Removed accounts are recreated from the snapshot.
  // Directory scan is limited to the undo folder we created.
  if (DIR* directory = opendir(dir.c_str())) {
    while (const dirent* item = readdir(directory)) {
      const std::string name = item->d_name;
      if (name.compare(0, 6, "added.") == 0) {
        const std::string user = name.substr(6);
        run_command({"userdel", "-r", user}, "");
      }
    }
    closedir(directory);
  }
  if (DIR* directory = opendir(dir.c_str())) {
    while (const dirent* item = readdir(directory)) {
      const std::string name = item->d_name;
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
      if (fields.size() < 7) {
        continue;
      }
      run_command({"useradd", "-M", "-o", "-u", fields[2], "-g", fields[3], "-d", fields[5], "-s",
                   fields[6], "-c", fields[4], fields[0]},
                  "");
      if (access((dir + "/shadow." + user).c_str(), F_OK) == 0) {
        restore_shadow_password(first_line(slurp(dir + "/shadow." + user)));
      }
      if (access((dir + "/homepath." + user).c_str(), F_OK) == 0) {
        const std::string home = first_line(slurp(dir + "/homepath." + user));
        rename((dir + "/home." + user).c_str(), home.c_str());
      }
      if (first_line(slurp(dir + "/sudo." + user)) == "yes") {
        run_command({"usermod", "-a", "-G", "sudo", user}, "");
      }
    }
    closedir(directory);
  }
  if (DIR* directory = opendir(dir.c_str())) {
    while (const dirent* item = readdir(directory)) {
      const std::string name = item->d_name;
      if (name.compare(0, 7, "shadow.") == 0 && getpwnam(name.substr(7).c_str()) != nullptr) {
        restore_shadow_password(first_line(slurp(dir + "/" + name)));
      } else if (name.compare(0, 5, "sudo.") == 0) {
        const std::string user = name.substr(5);
        const bool want = first_line(slurp(dir + "/" + name)) == "yes";
        if (want && !in_sudo(user)) {
          run_command({"usermod", "-a", "-G", "sudo", user}, "");
        } else if (!want && in_sudo(user)) {
          run_command({"gpasswd", "-d", user, "sudo"}, "");
        }
      } else if (name.compare(0, 7, "avatar.") == 0 && name.find(".missing") == std::string::npos) {
        const std::string user = name.substr(7);
        const passwd* entry = getpwnam(user.c_str());
        if (entry != nullptr && entry->pw_dir != nullptr) {
          const std::string dest = std::string(entry->pw_dir) + "/.face";
          if (access((dir + "/" + name + ".missing").c_str(), F_OK) == 0) {
            unlink(dest.c_str());
          } else {
            write_file(dest, slurp(dir + "/" + name), 0644);
            chown(dest.c_str(), entry->pw_uid, entry->pw_gid);
          }
        }
      } else if (name.compare(0, 8, "lightdm.") == 0 && name.find(".missing") == std::string::npos) {
        const char* paths[] = {"/etc/lightdm/lightdm.conf.d/50-lcos.conf", "/etc/lightdm/lightdm.conf"};
        const int index = std::atoi(name.c_str() + 8);
        if (index >= 0 && index < 2) {
          if (access((dir + "/" + name + ".missing").c_str(), F_OK) == 0) {
            unlink(paths[index]);
          } else {
            write_file(paths[index], slurp(dir + "/" + name), 0644);
          }
        }
      }
    }
    closedir(directory);
  }
  run_command({"rm", "-rf", dir}, "");
}

void commit_token(const std::string& token) {
  ensure_undo(token);
  run_command({"rm", "-rf", undo_dir(token)}, "");
}

}  // namespace

int main(int argc, char** argv) {
  if (geteuid() != 0) {
    fail("this program must run as root");
  }
  if (argc < 3) {
    fail("invalid request");
  }
  const std::string token = argv[1];
  const std::string action = argv[2];
  if (!lcos::valid_undo_token(token)) {
    fail("invalid request");
  }
  try {
    if (action == "revert") {
      revert_token(token);
    } else if (action == "commit") {
      commit_token(token);
    } else if (action == "set-timezone" && argc == 4) {
      action_timezone(token, argv[3]);
    } else if (action == "set-time" && argc == 5) {
      action_time(argv[3], argv[4]);
    } else if (action == "set-ntp" && argc == 4) {
      action_ntp(token, argv[3]);
    } else if (action == "set-locale" && argc == 7) {
      action_locale(token, {argv[3], argv[4], argv[5], argv[6]});
    } else if (action == "add-user" && argc == 6) {
      action_add_user(token, argv[3], argv[4], argv[5], first_line(read_stdin()));
    } else if (action == "remove-user" && argc == 4) {
      action_remove_user(token, argv[3]);
    } else if (action == "set-password" && argc == 5) {
      action_password(token, argv[3], argv[4], read_stdin());
    } else if (action == "set-type" && argc == 5) {
      action_type(token, argv[3], argv[4]);
    } else if (action == "set-real-name" && argc == 5) {
      action_real_name(token, argv[3], argv[4]);
    } else if (action == "set-avatar" && argc == 5) {
      action_avatar(token, argv[3], argv[4]);
    } else if (action == "set-autologin" && argc == 4) {
      action_autologin(token, argv[3]);
    } else {
      fail("invalid request");
    }
  } catch (const std::exception&) {
    fail("could not complete the change");
  }
  return 0;
}
