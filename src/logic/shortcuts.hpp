// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_SHORTCUTS_HPP
#define LCOS_LOGIC_SHORTCUTS_HPP

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace lcos {

inline constexpr const char* kShortcutsChannel = "xfce4-keyboard-shortcuts";
inline constexpr const char* kShortcutCustomBase = "/commands/custom";
inline constexpr const char* kShortcutDefaultBase = "/commands/default";

struct Shortcut {
  std::string accel;
  std::string command;
  bool startup_notify = false;
  bool custom = false;
};

// Property paths are full xfconf names such as
// /commands/custom/<Primary>t and /commands/custom/<Primary>t/startup-notify.
// `values` maps those paths to a string command or the literal "true"/"false"
// for the startup-notify child. Custom entries replace defaults that share
// an accelerator.
std::vector<Shortcut> merge_shortcuts(
    const std::map<std::string, std::string>& values);

std::string shortcut_command_path(bool custom, const std::string& accel);
std::string shortcut_notify_path(bool custom, const std::string& accel);

struct ShortcutConflict {
  std::string scope;
  std::string detail;
};

// Bindings for `accel` in application commands, window-manager shortcuts,
// and string properties on the xfwm4 channel. `ignore_command` and
// `ignore_feature` are the binding being edited, so it is not reported
// as a conflict with itself.
std::vector<ShortcutConflict> conflicting_shortcuts(
    const std::string& accel, const std::map<std::string, std::string>& commands,
    const std::map<std::string, std::string>& window_shortcuts,
    const std::map<std::string, std::string>& xfwm_properties,
    const std::string& ignore_command, const std::string& ignore_feature);

}  // namespace lcos

#endif
