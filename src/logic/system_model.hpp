// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_SYSTEM_MODEL_HPP
#define LCOS_LOGIC_SYSTEM_MODEL_HPP

#include <ctime>
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

// A shell the user can sign in with. nologin, false, and sync are not.
bool is_login_shell(const std::string& shell);
// uid is in the local human range and the shell can open a session.
bool is_local_human_account(unsigned uid, const std::string& shell, unsigned uid_min = 1000);

// Link text of /etc/localtime, absolute or relative, as a zone name.
// Empty when the target is not a zoneinfo path.
std::string timezone_from_localtime(const std::string& link_target);

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
// `sudo_gid` counts accounts whose primary group is sudo, not only gr_mem.
// Pass 0 when the primary group is unknown. Human accounts are uid >= uid_min,
// below 65534, and have a login shell.
std::vector<Account> human_accounts(const std::string& passwd_text,
                                    const std::string& sudo_members, unsigned uid_min,
                                    unsigned sudo_gid = 0);

bool is_last_administrator(const std::string& username,
                           const std::vector<Account>& accounts);

enum class HourClock { Unknown, Hour12, Hour24 };

HourClock classify_clock_format(const std::string& format);
const char* clock_format_string(HourClock clock);
// %T and %r keep their seconds. Choosing 12-hour or 24-hour must not rewrite
// them to a format that drops the seconds.
bool clock_format_has_seconds(const std::string& format);
std::string clock_format_for(HourClock clock, bool seconds);

// Wall clock after undoing an earlier adjustment. saved_wall and saved_mono
// were sampled together before the clock was changed. now_mono is the
// monotonic clock at revert time. The result is where the wall clock would
// be if the adjustment had never been made.
std::time_t restored_wall_clock(std::time_t saved_wall, std::time_t saved_mono,
                                std::time_t now_mono);

// Last uncommented autologin-user value in a seat section, or before any
// section. Whitespace around '=' is ignored. Empty means autologin is off
// or the key is absent.
std::string autologin_user_in_conf(const std::string& text);
// True when an uncommented autologin-user key exists. The value may be empty.
bool autologin_key_present(const std::string& text, std::string& value);
// Rewrites autologin-user only in the seat section that already has the key
// (or in the lines before any section). An empty user clears those keys.
// A file with no key gains the setting under [Seat:*]. Other sections stay.
std::string conf_with_autologin(const std::string& text, const std::string& user);

// LightDM reads these in order. The main file is last and overrides conf.d.
// 50-lcos.conf is inside the etc drop-in directory and is never a write target.
struct LightDmConfigRoot {
  std::string path;
  bool directory = false;
};
std::vector<LightDmConfigRoot> lightdm_config_roots();

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

// Last uncommented autologin-user across LightDM files, earlier files first.
// A later empty value turns automatic login off. Seat sections and legacy
// lines before a section are read. Whitespace around '=' is ignored.
std::string effective_autologin(const std::vector<std::string>& confs_in_order);

// Rewrites LANG, LC_TIME, LC_NUMERIC, and LC_MONETARY in an /etc/default/locale
// document. A regional value that is empty or equal to LANG is removed.
// LANGUAGE, other LC_* keys, comments, and blank lines stay.
std::string locale_file_with_values(const std::string& original, const std::string& lang,
                                    const std::string& lc_time, const std::string& lc_numeric,
                                    const std::string& lc_monetary);

// Puts a full shadow line back. A saved line with fewer than nine fields is
// refused. When the account is not in passwd, the file is left unchanged and
// ok is false: a short name:hash line is never appended.
struct ShadowRestore {
  bool ok = false;
  std::string text;
  std::string error;
};
ShadowRestore restore_shadow_line(const std::string& shadow_text, const std::string& saved_line,
                                  bool account_exists);

// True for an /etc/rcN.d start link such as S20chrony. Kill links are not.
bool sysv_start_link(const std::string& name, const std::string& service);
// True when any of runlevels 2 through 5 starts chrony. rc2 alone is not enough.
bool chrony_is_enabled();

// Names of [Session: ...] groups, in file order. These are the sessions
// xfce4-session stores inside its session file.
std::vector<std::string> saved_session_names(const std::string& keyfile_text);
// Removes one [Session: name] group. Every other group stays.
std::string keyfile_without_session(const std::string& keyfile_text, const std::string& name);

// Which privileged helper may perform an action. commit and revert are
// allowed on every domain; each domain only touches its own undo files.
enum class PrivDomain { System, Accounts, Locale };
PrivDomain priv_domain_for_action(const std::string& action);
bool priv_action_allowed(PrivDomain domain, const std::string& action);
bool undo_file_belongs_to(PrivDomain domain, const std::string& filename);
// A failed revert keeps the snapshot so the user can try again.
bool revert_keeps_snapshot(bool any_failure);

}  // namespace lcos

#endif
