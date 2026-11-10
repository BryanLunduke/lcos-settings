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

}  // namespace lcos

#endif
