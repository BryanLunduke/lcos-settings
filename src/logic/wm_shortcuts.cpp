// SPDX-License-Identifier: GPL-3.0-or-later

#include "wm_shortcuts.hpp"

#include <utility>

namespace lcos {
namespace {

const WmFeature kFeatures[] = {
    {"popup_menu_key", "Window operations menu"},
    {"up_key", "Up"},
    {"down_key", "Down"},
    {"left_key", "Left"},
    {"right_key", "Right"},
    {"cancel_key", "Cancel"},
    {"cycle_windows_key", "Cycle windows"},
    {"cycle_reverse_windows_key", "Cycle windows (reverse)"},
    {"switch_window_key", "Switch window for same application"},
    {"switch_application_key", "Switch application"},
    {"close_window_key", "Close window"},
    {"maximize_horiz_key", "Maximize window horizontally"},
    {"maximize_vert_key", "Maximize window vertically"},
    {"maximize_window_key", "Maximize window"},
    {"hide_window_key", "Hide window"},
    {"move_window_key", "Move window"},
    {"resize_window_key", "Resize window"},
    {"shade_window_key", "Shade window"},
    {"stick_window_key", "Stick window"},
    {"raise_window_key", "Raise window"},
    {"lower_window_key", "Lower window"},
    {"raiselower_window_key", "Raise or lower window"},
    {"fill_window_key", "Fill window"},
    {"fill_horiz_key", "Fill window horizontally"},
    {"fill_vert_key", "Fill window vertically"},
    {"above_key", "Toggle above"},
    {"fullscreen_key", "Toggle fullscreen"},
    {"move_window_to_monitor_down_key", "Move window to lower monitor"},
    {"move_window_to_monitor_left_key", "Move window to left monitor"},
    {"move_window_to_monitor_right_key", "Move window to right monitor"},
    {"move_window_to_monitor_up_key", "Move window to upper monitor"},
    {"move_window_up_workspace_key", "Move window to upper workspace"},
    {"move_window_down_workspace_key", "Move window to bottom workspace"},
    {"move_window_left_workspace_key", "Move window to left workspace"},
    {"move_window_right_workspace_key", "Move window to right workspace"},
    {"move_window_prev_workspace_key", "Move window to previous workspace"},
    {"move_window_next_workspace_key", "Move window to next workspace"},
    {"move_window_workspace_1_key", "Move window to workspace 1"},
    {"move_window_workspace_2_key", "Move window to workspace 2"},
    {"move_window_workspace_3_key", "Move window to workspace 3"},
    {"move_window_workspace_4_key", "Move window to workspace 4"},
    {"move_window_workspace_5_key", "Move window to workspace 5"},
    {"move_window_workspace_6_key", "Move window to workspace 6"},
    {"move_window_workspace_7_key", "Move window to workspace 7"},
    {"move_window_workspace_8_key", "Move window to workspace 8"},
    {"move_window_workspace_9_key", "Move window to workspace 9"},
    {"move_window_workspace_10_key", "Move window to workspace 10"},
    {"move_window_workspace_11_key", "Move window to workspace 11"},
    {"move_window_workspace_12_key", "Move window to workspace 12"},
    {"tile_up_key", "Tile window to the top"},
    {"tile_down_key", "Tile window to the bottom"},
    {"tile_left_key", "Tile window to the left"},
    {"tile_right_key", "Tile window to the right"},
    {"tile_up_left_key", "Tile window to the top-left"},
    {"tile_up_right_key", "Tile window to the top-right"},
    {"tile_down_left_key", "Tile window to the bottom-left"},
    {"tile_down_right_key", "Tile window to the bottom-right"},
    {"show_desktop_key", "Show desktop"},
    {"up_workspace_key", "Upper workspace"},
    {"down_workspace_key", "Bottom workspace"},
    {"left_workspace_key", "Left workspace"},
    {"right_workspace_key", "Right workspace"},
    {"prev_workspace_key", "Previous workspace"},
    {"next_workspace_key", "Next workspace"},
    {"workspace_1_key", "Workspace 1"},
    {"workspace_2_key", "Workspace 2"},
    {"workspace_3_key", "Workspace 3"},
    {"workspace_4_key", "Workspace 4"},
    {"workspace_5_key", "Workspace 5"},
    {"workspace_6_key", "Workspace 6"},
    {"workspace_7_key", "Workspace 7"},
    {"workspace_8_key", "Workspace 8"},
    {"workspace_9_key", "Workspace 9"},
    {"workspace_10_key", "Workspace 10"},
    {"workspace_11_key", "Workspace 11"},
    {"workspace_12_key", "Workspace 12"},
    {"add_workspace_key", "Add workspace"},
    {"add_adjacent_workspace_key", "Add adjacent workspace"},
    {"del_workspace_key", "Delete last workspace"},
    {"del_active_workspace_key", "Delete active workspace"},
    {nullptr, nullptr},
};

bool split_shortcut(const std::string& path, bool& custom, std::string& accel) {
  const std::string custom_prefix = std::string(kWmShortcutCustomBase) + "/";
  const std::string default_prefix = std::string(kWmShortcutDefaultBase) + "/";
  if (path.compare(0, custom_prefix.size(), custom_prefix) == 0) {
    custom = true;
    accel = path.substr(custom_prefix.size());
  } else if (path.compare(0, default_prefix.size(), default_prefix) == 0) {
    custom = false;
    accel = path.substr(default_prefix.size());
  } else {
    return false;
  }
  return !accel.empty() && accel.find('/') == std::string::npos;
}

struct Tables {
  std::map<std::string, std::string> defaults;
  std::map<std::string, std::string> customs;
  std::map<std::string, std::string> effective;
};

Tables tables_from(const std::map<std::string, std::string>& values) {
  Tables tables;
  for (const auto& entry : values) {
    bool custom = false;
    std::string accel;
    if (!split_shortcut(entry.first, custom, accel)) {
      continue;
    }
    if (custom) {
      tables.customs[accel] = entry.second;
    } else if (!entry.second.empty()) {
      tables.defaults[accel] = entry.second;
    }
  }
  tables.effective = tables.defaults;
  for (const auto& entry : tables.customs) {
    if (entry.second.empty()) {
      tables.effective.erase(entry.first);
    } else {
      tables.effective[entry.first] = entry.second;
    }
  }
  return tables;
}

void retire_accel(const Tables& tables, const std::string& feature, const std::string& accel,
                  std::vector<WmShortcutEdit>& edits) {
  if (accel.empty()) {
    return;
  }
  const auto custom = tables.customs.find(accel);
  const auto fallback = tables.defaults.find(accel);
  const bool has_custom = custom != tables.customs.end();
  const bool default_is_same =
      fallback != tables.defaults.end() && fallback->second == feature;
  WmShortcutEdit edit;
  edit.property = wm_shortcut_property(true, accel);
  if (default_is_same) {
    if (has_custom && custom->second.empty()) {
      return;
    }
    edit.action = WmShortcutAction::Set;
    edit.value = "";
    edits.push_back(edit);
    return;
  }
  if (has_custom) {
    edit.action = WmShortcutAction::Reset;
    edits.push_back(edit);
  }
}

}  // namespace

const WmFeature* wm_features() { return kFeatures; }

const char* wm_feature_label(const std::string& feature) {
  for (const WmFeature* item = kFeatures; item->feature != nullptr; ++item) {
    if (feature == item->feature) {
      return item->label;
    }
  }
  return nullptr;
}

std::string wm_shortcut_property(bool custom, const std::string& accel) {
  return std::string(custom ? kWmShortcutCustomBase : kWmShortcutDefaultBase) + "/" + accel;
}

std::vector<WmBinding> merge_wm_shortcuts(const std::map<std::string, std::string>& values) {
  const Tables tables = tables_from(values);
  std::map<std::string, std::string> feature_accel;
  std::map<std::string, bool> feature_custom;
  for (const auto& entry : tables.effective) {
    if (feature_accel.count(entry.second) != 0) {
      continue;
    }
    feature_accel[entry.second] = entry.first;
    feature_custom[entry.second] = tables.customs.count(entry.first) != 0 &&
                                   !tables.customs.at(entry.first).empty();
  }

  std::vector<WmBinding> rows;
  for (const WmFeature* item = kFeatures; item->feature != nullptr; ++item) {
    WmBinding row;
    row.feature = item->feature;
    row.label = item->label;
    const auto found = feature_accel.find(row.feature);
    if (found != feature_accel.end()) {
      row.accel = found->second;
      row.custom = feature_custom[row.feature];
      feature_accel.erase(found);
    }
    rows.push_back(std::move(row));
  }
  for (const auto& extra : feature_accel) {
    WmBinding row;
    row.feature = extra.first;
    row.label = extra.first;
    row.accel = extra.second;
    row.custom = feature_custom[extra.first];
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<WmShortcutEdit> plan_wm_shortcut(const std::map<std::string, std::string>& values,
                                             const std::string& feature,
                                             const std::string& new_accel) {
  const Tables tables = tables_from(values);
  std::string current;
  for (const auto& entry : tables.effective) {
    if (entry.second == feature) {
      current = entry.first;
      break;
    }
  }
  if (current == new_accel) {
    return {};
  }
  std::vector<WmShortcutEdit> edits;
  retire_accel(tables, feature, current, edits);
  if (!new_accel.empty()) {
    const auto existing = tables.customs.find(new_accel);
    if (existing == tables.customs.end() || existing->second != feature) {
      WmShortcutEdit edit;
      edit.action = WmShortcutAction::Set;
      edit.property = wm_shortcut_property(true, new_accel);
      edit.value = feature;
      edits.push_back(std::move(edit));
    }
  }
  return edits;
}

}  // namespace lcos
