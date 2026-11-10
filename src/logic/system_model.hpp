// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_SYSTEM_MODEL_HPP
#define LCOS_LOGIC_SYSTEM_MODEL_HPP

#include <map>
#include <string>
#include <vector>

namespace lcos {

bool valid_username(const std::string& name);
bool valid_real_name(const std::string& name);
// A password may contain spaces and colons. It may not be empty, longer than
// 128 bytes, or contain a newline or NUL.
bool valid_password(const std::string& password);
bool valid_timezone_name(const std::string& name);
bool valid_locale_name(const std::string& name);
bool valid_undo_token(const std::string& token);

std::string normalize_locale_name(const std::string& name);
std::string gecos_real_name(const std::string& gecos);

struct Account {
  std::string name;
  std::string real_name;
  unsigned uid = 0;
  unsigned gid = 0;
  std::string home;
  std::string shell;
  bool administrator = false;
};

// `sudo_members` is the comma-separated member list from the sudo group line.
// Human accounts are uid >= uid_min, below 65534, and not a nologin/false shell.
std::vector<Account> human_accounts(const std::string& passwd_text,
                                    const std::string& sudo_members, unsigned uid_min);

bool is_last_administrator(const std::string& username,
                           const std::vector<Account>& accounts);

enum class HourClock { Unknown, Hour12, Hour24 };

HourClock classify_clock_format(const std::string& format);
const char* clock_format_string(HourClock clock);

// Last uncommented autologin-user= value. Empty means autologin is off.
std::string autologin_user_in_conf(const std::string& text);
// Rewrites active autologin-user and autologin-user-timeout lines.
// An empty user removes them. Other lines stay.
std::string conf_with_autologin(const std::string& text, const std::string& user);

struct AutostartEntry {
  std::string id;
  std::string name;
  std::string comment;
  std::string command;
  bool enabled = true;
  bool removable = false;
  bool show_in_xfce = true;
  int run_hook = 0;
};

// Keys are desktop file basenames. Values are file text. A user file overlays
// the system file (user keys win). A partial override that only sets Hidden
// still keeps Type, Exec, and Name from the system file. Entries that are not
// applications, that hide from XFCE, or whose TryExec cannot be run are omitted.
std::vector<AutostartEntry> merge_autostart(
    const std::map<std::string, std::string>& system_files,
    const std::map<std::string, std::string>& user_files);

// Which keys a checkbox click should write. XFCE entries toggle Hidden.
// Entries for other desktops toggle X-XFCE-Autostart-Override and, when
// turning on, clear Hidden so the entry is not hidden everywhere.
struct AutostartToggle {
  bool set_hidden = false;
  bool hidden = false;
  bool set_override = false;
  bool override_on = false;
};

AutostartToggle autostart_toggle_keys(bool show_in_xfce, bool enabled_now);

std::string autostart_desktop_text(const std::string& name, const std::string& comment,
                                   const std::string& command, bool hidden, int run_hook);

// Generated locale names from `locale -a` output, normalized and sorted.
std::vector<std::string> generated_locale_names(const std::string& locale_a_output);

struct SupportedLocale {
  std::string name;
  std::string charset;
};

// Rows from /usr/share/i18n/SUPPORTED. Comments and blank lines are skipped.
std::vector<SupportedLocale> parse_supported_locales(const std::string& text);
const SupportedLocale* find_supported_locale(const std::vector<SupportedLocale>& locales,
                                            const std::string& name);

// Uncomments or appends the supported line in an /etc/locale.gen document.
// Returns false when `locale` is not one of `supported`.
bool enable_locale_in_gen(const std::string& locale_gen, const SupportedLocale& locale,
                          const std::vector<SupportedLocale>& supported, std::string& updated);

// Last uncommented autologin-user= across LightDM files, earlier files first.
// A later empty value turns automatic login off.
std::string effective_autologin(const std::vector<std::string>& confs_in_order);

}  // namespace lcos

#endif
