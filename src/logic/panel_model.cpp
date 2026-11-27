// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/panel_model.hpp"

#include <algorithm>
#include <cstdlib>

namespace lcos {
namespace {

int position_code(const std::string& position) {
  const auto mark = position.find("p=");
  if (mark == std::string::npos) {
    return -1;
  }
  const char* text = position.c_str() + mark + 2;
  if (*text == '\0' || (*text != '-' && (*text < '0' || *text > '9'))) {
    return -1;
  }
  return std::atoi(text);
}

}  // namespace

PanelEdge edge_from_position(const std::string& position) {
  switch (position_code(position)) {
    case 1:
    case 2:
    case 3:
      return PanelEdge::Top;
    case 10:
    case 11:
    case 12:
      return PanelEdge::Bottom;
    case 4:
    case 5:
    case 6:
      return PanelEdge::Left;
    case 7:
    case 8:
    case 9:
      return PanelEdge::Right;
    default:
      return PanelEdge::Unknown;
  }
}

std::string position_for_edge(PanelEdge edge) {
  int code = 11;
  switch (edge) {
    case PanelEdge::Top:
      code = 2;
      break;
    case PanelEdge::Bottom:
      code = 11;
      break;
    case PanelEdge::Left:
      code = 5;
      break;
    case PanelEdge::Right:
      code = 8;
      break;
    case PanelEdge::Unknown:
      code = 11;
      break;
  }
  return "p=" + std::to_string(code) + ";x=0;y=0";
}

const char* edge_label(PanelEdge edge) {
  switch (edge) {
    case PanelEdge::Top:
      return "Top";
    case PanelEdge::Bottom:
      return "Bottom";
    case PanelEdge::Left:
      return "Left";
    case PanelEdge::Right:
      return "Right";
    case PanelEdge::Unknown:
      return "Other";
  }
  return "Other";
}

int clamp_panel_size(int size) { return std::max(16, std::min(128, size)); }

int clamp_panel_length(int length) { return std::max(1, std::min(100, length)); }

int clamp_panel_opacity(int opacity) { return std::max(0, std::min(100, opacity)); }

int clamp_panel_autohide(int behavior) { return std::max(0, std::min(2, behavior)); }

PluginEdit shift_plugin(const std::vector<int>& ids, int index, int delta) {
  PluginEdit edit;
  edit.ids = ids;
  const int target = index + delta;
  if (delta == 0 || index < 0 || target < 0 || index >= static_cast<int>(ids.size()) ||
      target >= static_cast<int>(ids.size())) {
    return edit;
  }
  std::swap(edit.ids[static_cast<std::size_t>(index)], edit.ids[static_cast<std::size_t>(target)]);
  edit.ok = true;
  return edit;
}

PluginEdit drop_plugin(const std::vector<int>& ids, int index) {
  PluginEdit edit;
  edit.ids = ids;
  if (index < 0 || index >= static_cast<int>(ids.size())) {
    return edit;
  }
  edit.ids.erase(edit.ids.begin() + index);
  edit.ok = true;
  return edit;
}

int allocate_plugin_id(const std::vector<int>& used) {
  int next = 1;
  for (int id : used) {
    next = std::max(next, id + 1);
  }
  return next;
}

const std::vector<std::pair<std::string, std::string>>& panel_plugin_choices() {
  static const std::vector<std::pair<std::string, std::string>> choices = {
      {"separator", "Separator"},
      {"launcher", "Launcher"},
      {"tasklist", "Window Buttons"},
      {"systray", "Status Tray"},
      {"clock", "Clock"},
      {"showdesktop", "Show Desktop"},
      {"actions", "Action Buttons"},
      {"directorymenu", "Directory Menu"},
      {"windowmenu", "Window Menu"},
      {"pulseaudio", "PulseAudio"},
      {"notification-plugin", "Notifications"},
      {"power-manager-plugin", "Power Manager"},
      {"whiskermenu", "Whisker Menu"},
  };
  return choices;
}

std::string panel_plugin_title(const std::string& name) {
  for (const auto& choice : panel_plugin_choices()) {
    if (choice.first == name) {
      return choice.second;
    }
  }
  if (name == "pager") {
    return "Pager";
  }
  if (name.empty()) {
    return "Item";
  }
  return name;
}

}  // namespace lcos
