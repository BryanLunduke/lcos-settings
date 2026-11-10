// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_WM_SHORTCUTS_HPP
#define LCOS_LOGIC_WM_SHORTCUTS_HPP

#include <map>
#include <string>
#include <vector>

namespace lcos {

inline constexpr const char* kWmChannel = "xfwm4";
inline constexpr const char* kWmShortcutChannel = "xfce4-keyboard-shortcuts";
inline constexpr const char* kWmShortcutCustomBase = "/xfwm4/custom";
inline constexpr const char* kWmShortcutDefaultBase = "/xfwm4/default";

struct WmFeature {
  const char* feature;
  const char* label;
};

// The action list from xfwm4 4.20 (libxfce4ui xfce-shortcuts-xfwm4.c).
const WmFeature* wm_features();
const char* wm_feature_label(const std::string& feature);

std::string wm_shortcut_property(bool custom, const std::string& accel);

struct WmBinding {
  std::string feature;
  std::string label;
  std::string accel;
  bool custom = false;
};

// `values` maps full property paths to the stored string. A custom empty
// string hides the default accelerator. Custom entries replace defaults.
std::vector<WmBinding> merge_wm_shortcuts(const std::map<std::string, std::string>& values);

enum class WmShortcutAction { Set, Reset };

struct WmShortcutEdit {
  WmShortcutAction action = WmShortcutAction::Set;
  std::string property;
  std::string value;
};

// Plan the xfconf writes that point `feature` at `new_accel`. An empty
// accelerator clears the binding. Default shortcuts are hidden with an empty
// custom value so Cancel can restore them. The plan is empty when nothing
// would change.
std::vector<WmShortcutEdit> plan_wm_shortcut(const std::map<std::string, std::string>& values,
                                             const std::string& feature,
                                             const std::string& new_accel);

}  // namespace lcos

#endif
