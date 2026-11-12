// SPDX-License-Identifier: GPL-3.0-or-later

#include "system_model.hpp"

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>

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
  for (char c : name) {
    if (c == '/') {
      continue;
    }
    if (!is_ident_char(c)) {
      return false;
    }
  }
  // UTC and the other zoneinfo files without a slash are real zones.
  // ".." is already rejected, which is what the slash requirement was for.
  return true;
}

bool is_login_shell(const std::string& shell) {
  if (shell.empty()) {
    return false;
  }
  const std::string base = shell_base(shell);
  return base != "nologin" && base != "false" && base != "sync";
}

bool is_local_human_account(unsigned uid, const std::string& shell, unsigned uid_min) {
  return uid >= uid_min && uid < 65534 && is_login_shell(shell);
}

std::string timezone_from_localtime(const std::string& link_target) {
  const std::string marker = "zoneinfo/";
  const auto at = link_target.rfind(marker);
  if (at == std::string::npos) {
    return {};
  }
  std::string zone = link_target.substr(at + marker.size());
  while (!zone.empty() && zone.front() == '/') {
    zone.erase(zone.begin());
  }
  if (!valid_timezone_name(zone)) {
    return {};
  }
  return zone;
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

std::string gecos_with_full_name(const std::string& gecos, const std::string& full_name) {
  std::vector<std::string> fields;
  std::string field;
  for (char c : gecos) {
    if (c == ',') {
      fields.push_back(field);
      field.clear();
    } else {
      field.push_back(c);
    }
  }
  if (!gecos.empty()) {
    fields.push_back(field);
  }
  if (fields.empty()) {
    return full_name;
  }
  fields[0] = full_name;
  while (fields.size() > 1 && fields.back().empty()) {
    fields.pop_back();
  }
  std::string out;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    out += fields[i];
  }
  return out;
}

std::vector<Account> human_accounts(const std::string& passwd_text,
                                    const std::string& sudo_members, unsigned uid_min,
                                    unsigned sudo_gid) {
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
        if (is_local_human_account(uid, fields[6], uid_min) && valid_username(fields[0])) {
          Account account;
          account.name = fields[0];
          account.real_name = gecos_real_name(fields[4]);
          account.uid = uid;
          account.gid = gid;
          account.home = fields[5];
          account.shell = fields[6];
          account.administrator =
              admins.count(account.name) != 0 || (sudo_gid != 0 && gid == sudo_gid);
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

bool clock_format_has_seconds(const std::string& format) {
  return format == "%T" || format == "%H:%M:%S" || format == "%r" || format == "%I:%M:%S %p";
}

std::string clock_format_for(HourClock clock, bool seconds) {
  if (clock == HourClock::Hour12) {
    return seconds ? "%I:%M:%S %p" : "%I:%M %p";
  }
  return seconds ? "%T" : "%R";
}

std::time_t restored_wall_clock(std::time_t saved_wall, std::time_t saved_mono, std::time_t now_mono) {
  return saved_wall + (now_mono - saved_mono);
}

namespace {

std::string trim_copy_local(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin &&
         (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
    --end;
  }
  return text.substr(begin, end - begin);
}

bool seat_section(const std::string& section) {
  return section.compare(0, 5, "Seat:") == 0;
}

struct ConfLine {
  std::string raw;
  std::string section;
  bool header = false;
  bool key = false;
  std::string key_name;
  std::string value;
};

std::vector<ConfLine> parse_conf(const std::string& text) {
  std::vector<ConfLine> rows;
  std::string section;
  bool saw_section = false;
  std::string line;
  auto push = [&](const std::string& current) {
    ConfLine row;
    row.raw = current;
    row.section = section;
    const std::string trimmed = trim_copy_local(current);
    if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
      row.header = true;
      section = trim_copy_local(trimmed.substr(1, trimmed.size() - 2));
      saw_section = true;
      row.section = section;
      rows.push_back(std::move(row));
      return;
    }
    if (!trimmed.empty() && trimmed.front() != '#' && trimmed.front() != ';') {
      const auto eq = trimmed.find('=');
      if (eq != std::string::npos) {
        row.key = true;
        row.key_name = trim_copy_local(trimmed.substr(0, eq));
        std::string value = trim_copy_local(trimmed.substr(eq + 1));
        const auto cut = value.find_first_of(" \t#");
        if (cut != std::string::npos) {
          value.resize(cut);
        }
        row.value = value;
      }
    }
    (void)saw_section;
    rows.push_back(std::move(row));
  };
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      if (!(i == text.size() && line.empty() && !text.empty() && text.back() == '\n')) {
        push(line);
      }
      line.clear();
    } else {
      line.push_back(text[i]);
    }
  }
  return rows;
}

std::string join_conf(const std::vector<ConfLine>& rows) {
  std::string out;
  for (const auto& row : rows) {
    out += row.raw;
    out += '\n';
  }
  return out;
}

}  // namespace

bool autologin_key_present(const std::string& text, std::string& value) {
  value.clear();
  bool saw_section = false;
  bool legacy = false;
  bool star = false;
  bool specific = false;
  std::string legacy_value;
  std::string star_value;
  std::string specific_value;
  std::string section;
  for (const auto& row : parse_conf(text)) {
    if (row.header) {
      saw_section = true;
      section = row.section;
      continue;
    }
    if (!row.key || row.key_name != "autologin-user") {
      continue;
    }
    if (!saw_section) {
      legacy = true;
      legacy_value = row.value;
    } else if (section == "Seat:*") {
      star = true;
      star_value = row.value;
    } else if (seat_section(section)) {
      // A named seat overrides [Seat:*] for the seat LightDM is using.
      specific = true;
      specific_value = row.value;
    }
  }
  if (specific) {
    value = specific_value;
  } else if (star) {
    value = star_value;
  } else if (legacy) {
    value = legacy_value;
  }
  return legacy || star || specific;
}

std::string autologin_user_in_conf(const std::string& text) {
  std::string value;
  if (!autologin_key_present(text, value)) {
    return {};
  }
  return value;
}

std::string conf_with_autologin(const std::string& text, const std::string& user) {
  auto rows = parse_conf(text);
  bool saw_section = false;
  std::set<std::string> owners;
  bool legacy = false;
  std::string current;
  for (const auto& row : rows) {
    if (row.header) {
      saw_section = true;
      current = row.section;
      continue;
    }
    if (!row.key || row.key_name != "autologin-user") {
      continue;
    }
    if (!saw_section) {
      legacy = true;
    } else if (seat_section(current)) {
      owners.insert(current);
    }
  }
  const bool create_star = !user.empty() && !legacy && owners.empty();
  if (create_star) {
    owners.insert("Seat:*");
  }
  std::vector<ConfLine> out;
  bool in_owned = false;
  bool wrote = false;
  bool inserted = false;
  auto write_keys = [&]() {
    if (user.empty() || wrote) {
      return;
    }
    inserted = true;
    ConfLine user_line;
    user_line.raw = "autologin-user=" + user;
    out.push_back(user_line);
    ConfLine timeout;
    timeout.raw = "autologin-user-timeout=0";
    out.push_back(timeout);
    wrote = true;
  };
  saw_section = false;
  current.clear();
  for (const auto& row : rows) {
    if (row.header) {
      if (in_owned) {
        write_keys();
      }
      saw_section = true;
      current = row.section;
      in_owned = owners.count(current) != 0;
      wrote = false;
      out.push_back(row);
      if (in_owned) {
        write_keys();
      }
      continue;
    }
    const bool owned_here = (!saw_section && legacy) || (saw_section && in_owned);
    if (owned_here && row.key &&
        (row.key_name == "autologin-user" || row.key_name == "autologin-user-timeout")) {
      continue;
    }
    out.push_back(row);
  }
  if (!saw_section && legacy) {
    // Keys lived before any section. They were dropped above; put the new
    // pair back at the top when a user is being set.
    if (!user.empty()) {
      std::vector<ConfLine> prefixed;
      ConfLine user_line;
      user_line.raw = "autologin-user=" + user;
      prefixed.push_back(user_line);
      ConfLine timeout;
      timeout.raw = "autologin-user-timeout=0";
      prefixed.push_back(timeout);
      prefixed.insert(prefixed.end(), out.begin(), out.end());
      out.swap(prefixed);
    }
  } else if (create_star && !inserted) {
    ConfLine header;
    header.raw = "[Seat:*]";
    header.header = true;
    out.push_back(header);
    ConfLine user_line;
    user_line.raw = "autologin-user=" + user;
    out.push_back(user_line);
    ConfLine timeout;
    timeout.raw = "autologin-user-timeout=0";
    out.push_back(timeout);
  }
  return join_conf(out);
}

std::vector<LightDmConfigRoot> lightdm_config_roots() {
  // Same order LightDM uses. /etc/lightdm/lightdm.conf is loaded after every
  // drop-in, so a key set there wins over 60-lcos-settings.conf.
  return {{"/usr/share/lightdm/lightdm.conf", false},
          {"/usr/share/lightdm/lightdm.conf.d", true},
          {"/etc/xdg/lightdm/lightdm.conf.d", true},
          {"/etc/lightdm/lightdm.conf.d", true},
          {"/etc/lightdm/lightdm.conf", false}};
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
      // A file that already ends in a newline must not gain a blank line.
      const bool trailing_break = i == locale_gen.size() && line.empty();
      if (!trailing_break) {
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
    std::string one;
    if (autologin_key_present(text, one)) {
      value = one;
      seen = true;
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

std::string locale_file_with_values(const std::string& original, const std::string& lang,
                                    const std::string& lc_time, const std::string& lc_numeric,
                                    const std::string& lc_monetary) {
  const std::string keys[] = {"LANG", "LC_TIME", "LC_NUMERIC", "LC_MONETARY"};
  const std::string values[] = {
      lang,
      (!lc_time.empty() && lc_time != lang) ? lc_time : std::string(),
      (!lc_numeric.empty() && lc_numeric != lang) ? lc_numeric : std::string(),
      (!lc_monetary.empty() && lc_monetary != lang) ? lc_monetary : std::string(),
  };
  bool written[4] = {false, false, false, false};
  std::string out;
  std::string line;
  auto consider = [&](const std::string& current) {
    const std::string trimmed = trim_copy_local(current);
    if (!trimmed.empty() && trimmed.front() != '#') {
      const auto eq = trimmed.find('=');
      if (eq != std::string::npos) {
        const std::string key = trim_copy_local(trimmed.substr(0, eq));
        for (int i = 0; i < 4; ++i) {
          if (key == keys[i]) {
            written[i] = true;
            if (!values[i].empty()) {
              out += keys[i] + "=" + values[i] + "\n";
            }
            return;
          }
        }
      }
    }
    out += current;
    out += '\n';
  };
  for (std::size_t i = 0; i <= original.size(); ++i) {
    if (i == original.size() || original[i] == '\n') {
      if (!(i == original.size() && line.empty() && !original.empty() && original.back() == '\n')) {
        consider(line);
      }
      line.clear();
    } else {
      line.push_back(original[i]);
    }
  }
  for (int i = 0; i < 4; ++i) {
    if (!written[i] && !values[i].empty()) {
      out += keys[i] + "=" + values[i] + "\n";
    }
  }
  return out;
}

namespace {

std::vector<std::string> split_fields(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  for (char c : line) {
    if (c == ':') {
      fields.push_back(field);
      field.clear();
    } else if (c != '\r') {
      field.push_back(c);
    }
  }
  fields.push_back(field);
  return fields;
}

std::string join_fields(const std::vector<std::string>& fields) {
  std::string out;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i != 0) {
      out.push_back(':');
    }
    out += fields[i];
  }
  return out;
}

// glibc stores a blank shadow flag as ~0ul. Writing that decimal makes
// getent, pwck, and su reject the account.
bool shadow_flag_text_is_sentinel(const std::string& text) {
  if (text.empty()) {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long value = std::strtoul(text.c_str(), &end, 10);
  if (end == text.c_str() || (end != nullptr && *end != '\0')) {
    return false;
  }
  return value == std::numeric_limits<unsigned long>::max();
}

std::string shadow_number(long value) { return value < 0 ? std::string() : std::to_string(value); }

}  // namespace

std::string shadow_flag_text(unsigned long flag) {
  if (flag == 0 || flag == std::numeric_limits<unsigned long>::max()) {
    return {};
  }
  return std::to_string(flag);
}

std::string format_shadow_record(const ShadowRecord& record) {
  std::ostringstream out;
  out << record.name << ':' << record.hash << ':' << shadow_number(record.last_change) << ':'
      << shadow_number(record.minimum) << ':' << shadow_number(record.maximum) << ':'
      << shadow_number(record.warning) << ':' << shadow_number(record.inactive) << ':'
      << shadow_number(record.expire) << ':' << shadow_flag_text(record.flag);
  return out.str();
}

std::string shadow_line_for_user(const std::string& shadow_text, const std::string& user) {
  if (user.empty() || user.find(':') != std::string::npos || user.find('\n') != std::string::npos) {
    return {};
  }
  const std::string prefix = user + ":";
  std::string line;
  for (std::size_t i = 0; i <= shadow_text.size(); ++i) {
    if (i == shadow_text.size() || shadow_text[i] == '\n') {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.compare(0, prefix.size(), prefix) == 0) {
        return line;
      }
      line.clear();
    } else {
      line.push_back(shadow_text[i]);
    }
  }
  return {};
}

std::string normalize_shadow_line(const std::string& saved_line) {
  const std::string saved = trim_copy_local(saved_line);
  auto fields = split_fields(saved);
  if (fields.size() < 9 || !shadow_flag_text_is_sentinel(fields.back())) {
    return saved;
  }
  fields.back().clear();
  return join_fields(fields);
}

std::string passwd_field(const std::string& line, int index) {
  if (index < 0) {
    return {};
  }
  int current = 0;
  std::string field;
  for (char c : line) {
    if (c == '\n' || c == '\r') {
      break;
    }
    if (c == ':') {
      if (current == index) {
        return field;
      }
      field.clear();
      ++current;
      continue;
    }
    field.push_back(c);
  }
  return current == index ? field : std::string();
}

int passwd_field_count(const std::string& line) {
  if (line.empty() || line[0] == '\n') {
    return 0;
  }
  int count = 1;
  for (char c : line) {
    if (c == '\n' || c == '\r') {
      break;
    }
    if (c == ':') {
      ++count;
    }
  }
  return count;
}

ShadowRestore restore_shadow_line(const std::string& shadow_text, const std::string& saved_line,
                                  bool account_exists) {
  ShadowRestore result;
  result.text = shadow_text;
  const std::string saved = normalize_shadow_line(saved_line);
  const auto fields = split_fields(saved);
  if (fields.size() < 9 || fields[0].empty()) {
    result.error = "the saved password entry is incomplete";
    return result;
  }
  if (!account_exists) {
    result.error = "the account was not restored";
    return result;
  }
  const std::string& name = fields[0];
  std::string out;
  std::string line;
  bool replaced = false;
  auto consider = [&](const std::string& current) {
    if (current.compare(0, name.size() + 1, name + ":") == 0) {
      out += saved;
      replaced = true;
    } else if (!current.empty()) {
      out += current;
    } else {
      return;
    }
    out += '\n';
  };
  for (std::size_t i = 0; i <= shadow_text.size(); ++i) {
    if (i == shadow_text.size() || shadow_text[i] == '\n') {
      if (!(i == shadow_text.size() && line.empty())) {
        consider(line);
      }
      line.clear();
    } else {
      line.push_back(shadow_text[i]);
    }
  }
  if (!replaced) {
    if (!out.empty() && out.back() != '\n') {
      out += '\n';
    }
    out += saved;
    out += '\n';
  }
  result.ok = true;
  result.text = out;
  return result;
}

bool sysv_start_link(const std::string& name, const std::string& service) {
  if (name.empty() || service.empty() || name.front() != 'S' || name.size() <= service.size()) {
    return false;
  }
  return name.compare(name.size() - service.size(), service.size(), service) == 0;
}

bool chrony_is_enabled() {
  const char* dirs[] = {"/etc/rc2.d", "/etc/rc3.d", "/etc/rc4.d", "/etc/rc5.d"};
  for (const char* path : dirs) {
    DIR* directory = opendir(path);
    if (directory == nullptr) {
      continue;
    }
    bool enabled = false;
    while (const dirent* item = readdir(directory)) {
      if (sysv_start_link(item->d_name, "chrony")) {
        enabled = true;
        break;
      }
    }
    closedir(directory);
    if (enabled) {
      return true;
    }
  }
  return false;
}

namespace {

bool session_header(const std::string& line, std::string& name) {
  const std::string trimmed = trim_copy_local(line);
  const std::string prefix = "[Session: ";
  if (trimmed.size() < prefix.size() + 1 || trimmed.compare(0, prefix.size(), prefix) != 0 ||
      trimmed.back() != ']') {
    return false;
  }
  name = trimmed.substr(prefix.size(), trimmed.size() - prefix.size() - 1);
  return !name.empty();
}

}  // namespace

std::vector<std::string> saved_session_names(const std::string& keyfile_text) {
  std::vector<std::string> names;
  std::string line;
  for (std::size_t i = 0; i <= keyfile_text.size(); ++i) {
    if (i == keyfile_text.size() || keyfile_text[i] == '\n') {
      std::string name;
      if (session_header(line, name)) {
        names.push_back(name);
      }
      line.clear();
    } else {
      line.push_back(keyfile_text[i]);
    }
  }
  return names;
}

std::string keyfile_without_session(const std::string& keyfile_text, const std::string& name) {
  std::string out;
  std::string line;
  bool skipping = false;
  auto consider = [&](const std::string& current) {
    const std::string trimmed = trim_copy_local(current);
    if (!trimmed.empty() && trimmed.front() == '[') {
      std::string header;
      skipping = session_header(current, header) && header == name;
    }
    if (!skipping) {
      out += current;
      out += '\n';
    }
  };
  for (std::size_t i = 0; i <= keyfile_text.size(); ++i) {
    if (i == keyfile_text.size() || keyfile_text[i] == '\n') {
      if (!(i == keyfile_text.size() && line.empty() && !keyfile_text.empty() &&
            keyfile_text.back() == '\n')) {
        consider(line);
      }
      line.clear();
    } else {
      line.push_back(keyfile_text[i]);
    }
  }
  return out;
}

PrivDomain priv_domain_for_action(const std::string& action) {
  if (action == "set-locale") {
    return PrivDomain::Locale;
  }
  if (action == "add-user" || action == "remove-user" || action == "set-password" ||
      action == "set-type" || action == "set-real-name" || action == "set-avatar" ||
      action == "set-autologin") {
    return PrivDomain::Accounts;
  }
  return PrivDomain::System;
}

bool priv_action_allowed(PrivDomain domain, const std::string& action) {
  if (action == "commit" || action == "revert") {
    return true;
  }
  return priv_domain_for_action(action) == domain;
}

bool undo_file_belongs_to(PrivDomain domain, const std::string& filename) {
  const auto starts = [](const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
  };
  const bool system = filename == "localtime" || filename == "timezone" ||
                      filename == "timezone.missing" || filename == "ntp" ||
                      filename == "clock-wall" || filename == "clock-mono";
  const bool locale = filename == "locale" || filename == "locale.missing" ||
                      filename == "locale.dir" || starts(filename, "locale.gen") ||
                      starts(filename, "locale.archive");
  const bool accounts = starts(filename, "added.") || starts(filename, "passwd.") ||
                        starts(filename, "shadow.") || starts(filename, "sudo.") ||
                        starts(filename, "avatar.") || starts(filename, "lightdm.") ||
                        starts(filename, "group.") || starts(filename, "groups.") ||
                        starts(filename, "homepath.") || starts(filename, "home.");
  if (domain == PrivDomain::System) {
    return system;
  }
  if (domain == PrivDomain::Locale) {
    return locale;
  }
  return accounts;
}

bool revert_keeps_snapshot(bool any_failure) { return any_failure; }

}  // namespace lcos
