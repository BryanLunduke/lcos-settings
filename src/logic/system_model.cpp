// SPDX-License-Identifier: GPL-3.0-or-later

#include "system_model.hpp"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace lcos {
namespace {

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '+' ||
         c == '.';
}

std::string lower_copy(std::string text) {
  for (char& c : text) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return text;
}

std::vector<std::string> split_list(const std::string& text) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : text) {
    if (c == ',' || c == ';') {
      if (!current.empty()) {
        parts.push_back(current);
      }
      current.clear();
    } else if (c != ' ' && c != '\t') {
      current.push_back(c);
    }
  }
  if (!current.empty()) {
    parts.push_back(current);
  }
  return parts;
}

std::map<std::string, std::string> desktop_keys(const std::string& text) {
  std::map<std::string, std::string> keys;
  bool in_group = false;
  std::string line;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line == "[Desktop Entry]") {
        in_group = true;
      } else if (!line.empty() && line.front() == '[') {
        in_group = false;
      } else if (in_group) {
        const auto eq = line.find('=');
        if (eq != std::string::npos && (line.empty() || line[0] != '#')) {
          keys[line.substr(0, eq)] = line.substr(eq + 1);
        }
      }
      line.clear();
    } else {
      line.push_back(text[i]);
    }
  }
  return keys;
}

bool truthy(const std::string& value) {
  const std::string lower = lower_copy(value);
  return lower == "true" || lower == "1" || lower == "yes";
}

bool list_has_xfce(const std::string& value) {
  for (const auto& part : split_list(value)) {
    if (lower_copy(part) == "xfce") {
      return true;
    }
  }
  return false;
}

std::string shell_base(const std::string& shell) {
  const auto slash = shell.find_last_of('/');
  return slash == std::string::npos ? shell : shell.substr(slash + 1);
}

bool command_runnable(const std::string& command) {
  std::string bin;
  for (char c : command) {
    if (c == ' ' || c == '\t') {
      break;
    }
    bin.push_back(c);
  }
  if (bin.empty()) {
    return false;
  }
  if (bin.find('/') != std::string::npos) {
    return access(bin.c_str(), X_OK) == 0;
  }
  const char* path = std::getenv("PATH");
  if (path == nullptr) {
    return false;
  }
  std::string dir;
  for (std::size_t i = 0; i <= std::strlen(path); ++i) {
    if (path[i] == ':' || path[i] == '\0') {
      const std::string full = (dir.empty() ? std::string(".") : dir) + "/" + bin;
      if (access(full.c_str(), X_OK) == 0) {
        return true;
      }
      dir.clear();
      if (path[i] == '\0') {
        break;
      }
    } else {
      dir.push_back(path[i]);
    }
  }
  return false;
}

}  // namespace

bool valid_username(const std::string& name) {
  if (name.empty() || name.size() > 32) {
    return false;
  }
  const char first = name.front();
  if (!(std::islower(static_cast<unsigned char>(first)) || first == '_')) {
    return false;
  }
  for (char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (!(std::islower(u) || std::isdigit(u) || c == '_' || c == '-')) {
      return false;
    }
  }
  return name != "root" && name != "nobody";
}

bool valid_real_name(const std::string& name) {
  if (name.size() > 128) {
    return false;
  }
  for (unsigned char c : name) {
    if (c < 0x20 || c == 0x7f || c == ':' || c == ',' || c == '=' || c == '\n') {
      return false;
    }
  }
  return true;
}

bool valid_password(const std::string& password) {
  if (password.empty() || password.size() > 128) {
    return false;
  }
  return password.find('\n') == std::string::npos && password.find('\r') == std::string::npos &&
         password.find('\0') == std::string::npos;
}

bool valid_timezone_name(const std::string& name) {
  if (name.empty() || name.size() > 80 || name.front() == '/' || name.back() == '/') {
    return false;
  }
  if (name.find("..") != std::string::npos || name.find('\\') != std::string::npos) {
    return false;
  }
  bool slash = false;
  for (char c : name) {
    if (c == '/') {
      slash = true;
      continue;
    }
    if (!is_ident_char(c)) {
      return false;
    }
  }
  return slash;
}

bool valid_locale_name(const std::string& name) {
  if (name == "C" || name == "C.UTF-8" || name == "POSIX") {
    return true;
  }
  if (name.size() < 2 || name.size() > 64) {
    return false;
  }
  for (char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (!(std::isalnum(u) || c == '_' || c == '.' || c == '@' || c == '-')) {
      return false;
    }
  }
  return true;
}

bool valid_undo_token(const std::string& token) {
  if (token.size() != 32) {
    return false;
  }
  for (char c : token) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

std::string normalize_locale_name(const std::string& name) {
  std::string out = name;
  const auto dot = out.rfind('.');
  if (dot != std::string::npos && lower_copy(out.substr(dot + 1)) == "utf8") {
    out.replace(dot + 1, std::string::npos, "UTF-8");
  }
  return out;
}

std::string gecos_real_name(const std::string& gecos) {
  const auto comma = gecos.find(',');
  return comma == std::string::npos ? gecos : gecos.substr(0, comma);
}

std::vector<Account> human_accounts(const std::string& passwd_text,
                                    const std::string& sudo_members, unsigned uid_min) {
  std::set<std::string> admins;
  for (const auto& member : split_list(sudo_members)) {
    admins.insert(member);
  }
  std::vector<Account> accounts;
  std::string line;
  for (std::size_t i = 0; i <= passwd_text.size(); ++i) {
    if (i != passwd_text.size() && passwd_text[i] != '\n') {
      line.push_back(passwd_text[i]);
      continue;
    }
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty() && line[0] != '#') {
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
      if (fields.size() >= 7) {
        unsigned uid = 0;
        unsigned gid = 0;
        try {
          uid = static_cast<unsigned>(std::stoul(fields[2]));
          gid = static_cast<unsigned>(std::stoul(fields[3]));
        } catch (const std::exception&) {
          uid = 0;
        }
        const std::string shell = shell_base(fields[6]);
        const bool human_shell = shell != "nologin" && shell != "false" && shell != "sync";
        if (uid >= uid_min && uid < 65534 && human_shell && valid_username(fields[0])) {
          Account account;
          account.name = fields[0];
          account.real_name = gecos_real_name(fields[4]);
          account.uid = uid;
          account.gid = gid;
          account.home = fields[5];
          account.shell = fields[6];
          account.administrator = admins.count(account.name) != 0;
          accounts.push_back(std::move(account));
        }
      }
    }
    line.clear();
  }
  std::sort(accounts.begin(), accounts.end(),
            [](const Account& a, const Account& b) { return a.name < b.name; });
  return accounts;
}

bool is_last_administrator(const std::string& username, const std::vector<Account>& accounts) {
  int admins = 0;
  bool target = false;
  for (const auto& account : accounts) {
    if (account.administrator) {
      ++admins;
      if (account.name == username) {
        target = true;
      }
    }
  }
  return target && admins <= 1;
}

HourClock classify_clock_format(const std::string& format) {
  if (format == "%R" || format == "%T" || format == "%H:%M" || format == "%H:%M:%S") {
    return HourClock::Hour24;
  }
  if (format == "%r" || format == "%I:%M %p" || format == "%l:%M %p" || format == "%I:%M:%S %p") {
    return HourClock::Hour12;
  }
  return HourClock::Unknown;
}

const char* clock_format_string(HourClock clock) {
  return clock == HourClock::Hour12 ? "%I:%M %p" : "%R";
}

std::string autologin_user_in_conf(const std::string& text) {
  std::string found;
  bool seen = false;
  std::string line;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i != text.size() && text[i] != '\n') {
      line.push_back(text[i]);
      continue;
    }
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
      ++start;
    }
    const std::string trimmed = line.substr(start);
    const std::string key = "autologin-user=";
    if (!trimmed.empty() && trimmed[0] != '#' && trimmed.compare(0, key.size(), key) == 0) {
      found = trimmed.substr(key.size());
      const auto cut = found.find_first_of(" \t#");
      if (cut != std::string::npos) {
        found.resize(cut);
      }
      seen = true;
    }
    line.clear();
  }
  return seen ? found : std::string();
}

std::string conf_with_autologin(const std::string& text, const std::string& user) {
  std::string out;
  bool wrote_user = user.empty();
  bool wrote_timeout = user.empty();
  std::string line;
  auto flush = [&](const std::string& current) {
    std::size_t start = 0;
    while (start < current.size() && (current[start] == ' ' || current[start] == '\t')) {
      ++start;
    }
    const std::string trimmed = current.substr(start);
    if (!trimmed.empty() && trimmed[0] != '#' &&
        (trimmed.compare(0, 15, "autologin-user=") == 0 ||
         trimmed.compare(0, 23, "autologin-user-timeout=") == 0)) {
      return;
    }
    out += current;
    out += '\n';
  };
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      if (!(i == text.size() && line.empty() && text.empty())) {
        flush(line);
      }
      line.clear();
    } else {
      line.push_back(text[i]);
    }
  }
  if (!user.empty()) {
    if (out.empty() || out.find("[Seat:") == std::string::npos) {
      if (!out.empty() && out.back() != '\n') {
        out += '\n';
      }
      out += "[Seat:*]\n";
    }
    out += "autologin-user=" + user + "\n";
    out += "autologin-user-timeout=0\n";
    wrote_user = true;
    wrote_timeout = true;
  }
  (void)wrote_user;
  (void)wrote_timeout;
  return out;
}

std::vector<AutostartEntry> merge_autostart(
    const std::map<std::string, std::string>& system_files,
    const std::map<std::string, std::string>& user_files) {
  std::set<std::string> ids;
  for (const auto& entry : system_files) {
    ids.insert(entry.first);
  }
  for (const auto& entry : user_files) {
    ids.insert(entry.first);
  }
  std::vector<AutostartEntry> rows;
  for (const auto& id : ids) {
    const auto user = user_files.find(id);
    const auto system = system_files.find(id);
    std::map<std::string, std::string> keys;
    if (system != system_files.end()) {
      keys = desktop_keys(system->second);
    }
    if (user != user_files.end()) {
      for (const auto& entry : desktop_keys(user->second)) {
        keys[entry.first] = entry.second;
      }
    }
    const auto type = keys.find("Type");
    if (type == keys.end() || lower_copy(type->second) != "application") {
      continue;
    }
    if (list_has_xfce(keys.count("NotShowIn") ? keys.at("NotShowIn") : "")) {
      continue;
    }
    if (keys.count("TryExec") != 0 && !command_runnable(keys.at("TryExec"))) {
      continue;
    }
    AutostartEntry row;
    row.id = id;
    row.name = keys.count("Name") ? keys.at("Name") : id;
    row.comment = keys.count("Comment") ? keys.at("Comment") : "";
    row.command = keys.count("Exec") ? keys.at("Exec") : "";
    const bool only = keys.count("OnlyShowIn") != 0;
    row.show_in_xfce = !only || list_has_xfce(keys.at("OnlyShowIn"));
    const bool hidden = keys.count("Hidden") && truthy(keys.at("Hidden"));
    const bool override_on =
        keys.count("X-XFCE-Autostart-Override") && truthy(keys.at("X-XFCE-Autostart-Override"));
    row.enabled = row.show_in_xfce ? !hidden : override_on;
    row.removable = system == system_files.end();
    if (keys.count("RunHook")) {
      try {
        row.run_hook = std::stoi(keys.at("RunHook"));
      } catch (const std::exception&) {
        row.run_hook = 0;
      }
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

AutostartToggle autostart_toggle_keys(bool show_in_xfce, bool enabled_now) {
  AutostartToggle toggle;
  if (show_in_xfce) {
    toggle.set_hidden = true;
    toggle.hidden = enabled_now;
    return toggle;
  }
  toggle.set_override = true;
  toggle.override_on = !enabled_now;
  if (!enabled_now) {
    toggle.set_hidden = true;
    toggle.hidden = false;
  }
  return toggle;
}

std::string autostart_desktop_text(const std::string& name, const std::string& comment,
                                   const std::string& command, bool hidden, int run_hook) {
  std::string text = "[Desktop Entry]\nType=Application\n";
  text += "Name=" + name + "\n";
  text += "Comment=" + comment + "\n";
  text += "Exec=" + command + "\n";
  text += std::string("Hidden=") + (hidden ? "true" : "false") + "\n";
  text += "RunHook=" + std::to_string(run_hook) + "\n";
  text += "X-XFCE-Autostart-Override=true\n";
  return text;
}

std::vector<SupportedLocale> parse_supported_locales(const std::string& text) {
  std::vector<SupportedLocale> locales;
  std::string line;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i != text.size() && text[i] != '\n') {
      line.push_back(text[i]);
      continue;
    }
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
      ++start;
    }
    if (start < line.size() && line[start] != '#') {
      const auto gap = line.find_first_of(" \t", start);
      if (gap != std::string::npos) {
        SupportedLocale locale;
        locale.name = line.substr(start, gap - start);
        std::size_t charset = gap;
        while (charset < line.size() && (line[charset] == ' ' || line[charset] == '\t')) {
          ++charset;
        }
        const auto end = line.find_first_of(" \t#", charset);
        locale.charset = line.substr(charset, end == std::string::npos ? std::string::npos : end - charset);
        if (valid_locale_name(locale.name) && !locale.charset.empty()) {
          locales.push_back(std::move(locale));
        }
      }
    }
    line.clear();
  }
  return locales;
}

const SupportedLocale* find_supported_locale(const std::vector<SupportedLocale>& locales,
                                            const std::string& name) {
  for (const auto& locale : locales) {
    if (locale.name == name) {
      return &locale;
    }
  }
  return nullptr;
}

bool enable_locale_in_gen(const std::string& locale_gen, const SupportedLocale& locale,
                          const std::vector<SupportedLocale>& supported, std::string& updated) {
  if (find_supported_locale(supported, locale.name) == nullptr || locale.charset.empty()) {
    return false;
  }
  const std::string wanted = locale.name + " " + locale.charset;
  std::string out;
  bool enabled = false;
  bool uncommented = false;
  std::string line;
  auto consider = [&](const std::string& current) {
    std::size_t start = 0;
    while (start < current.size() && (current[start] == ' ' || current[start] == '\t')) {
      ++start;
    }
    std::string body = current.substr(start);
    bool comment = false;
    if (!body.empty() && body[0] == '#') {
      comment = true;
      body.erase(0, 1);
      std::size_t body_start = 0;
      while (body_start < body.size() && (body[body_start] == ' ' || body[body_start] == '\t')) {
        ++body_start;
      }
      body = body.substr(body_start);
    }
    const auto end = body.find_first_of(" \t#");
    const std::string first = body.substr(0, end);
    std::string charset;
    if (end != std::string::npos) {
      std::size_t charset_at = end;
      while (charset_at < body.size() && (body[charset_at] == ' ' || body[charset_at] == '\t')) {
        ++charset_at;
      }
      const auto charset_end = body.find_first_of(" \t#", charset_at);
      charset = body.substr(charset_at, charset_end == std::string::npos ? std::string::npos
                                                                        : charset_end - charset_at);
    }
    if (first == locale.name && charset == locale.charset) {
      if (!comment) {
        enabled = true;
        out += current + "\n";
      } else if (!uncommented && !enabled) {
        out += wanted + "\n";
        uncommented = true;
      } else {
        out += current + "\n";
      }
      return;
    }
    out += current + "\n";
  };
  for (std::size_t i = 0; i <= locale_gen.size(); ++i) {
    if (i == locale_gen.size() || locale_gen[i] == '\n') {
      if (!(i == locale_gen.size() && line.empty() && locale_gen.empty())) {
        consider(line);
      }
      line.clear();
    } else {
      line.push_back(locale_gen[i]);
    }
  }
  if (!enabled && !uncommented) {
    if (!out.empty() && out.back() != '\n') {
      out += '\n';
    }
    out += wanted + "\n";
  }
  updated = out;
  return true;
}

std::string effective_autologin(const std::vector<std::string>& confs_in_order) {
  bool seen = false;
  std::string value;
  for (const auto& text : confs_in_order) {
    std::string line;
    for (std::size_t i = 0; i <= text.size(); ++i) {
      if (i != text.size() && text[i] != '\n') {
        line.push_back(text[i]);
        continue;
      }
      std::size_t start = 0;
      while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
        ++start;
      }
      const std::string trimmed = line.substr(start);
      const std::string key = "autologin-user=";
      if (!trimmed.empty() && trimmed[0] != '#' && trimmed.compare(0, key.size(), key) == 0) {
        value = trimmed.substr(key.size());
        const auto cut = value.find_first_of(" \t#");
        if (cut != std::string::npos) {
          value.resize(cut);
        }
        seen = true;
      }
      line.clear();
    }
  }
  return seen ? value : std::string();
}

std::vector<std::string> generated_locale_names(const std::string& locale_a_output) {
  std::set<std::string> names;
  std::string line;
  for (std::size_t i = 0; i <= locale_a_output.size(); ++i) {
    if (i == locale_a_output.size() || locale_a_output[i] == '\n') {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (!line.empty() && line[0] != '#') {
        const std::string normalized = normalize_locale_name(line);
        if (valid_locale_name(normalized)) {
          names.insert(normalized);
        }
      }
      line.clear();
    } else {
      line.push_back(locale_a_output[i]);
    }
  }
  return std::vector<std::string>(names.begin(), names.end());
}

}  // namespace lcos
