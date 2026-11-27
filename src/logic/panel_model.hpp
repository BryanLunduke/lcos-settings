// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_PANEL_MODEL_HPP
#define LCOS_LOGIC_PANEL_MODEL_HPP

#include <string>
#include <utility>
#include <vector>

namespace lcos {

// Screen-edge codes stored in xfce4-panel's position string (p=N;x=;y=).
enum class PanelEdge { Top, Bottom, Left, Right, Unknown };

PanelEdge edge_from_position(const std::string& position);
std::string position_for_edge(PanelEdge edge);
const char* edge_label(PanelEdge edge);

int clamp_panel_size(int size);
int clamp_panel_length(int length);
int clamp_panel_opacity(int opacity);
int clamp_panel_autohide(int behavior);

struct PluginEdit {
  bool ok = false;
  std::vector<int> ids;
};

PluginEdit shift_plugin(const std::vector<int>& ids, int index, int delta);
PluginEdit drop_plugin(const std::vector<int>& ids, int index);
int allocate_plugin_id(const std::vector<int>& used);

// Plugins offered by Add. The pager is omitted; workspaces stay hidden.
const std::vector<std::pair<std::string, std::string>>& panel_plugin_choices();
std::string panel_plugin_title(const std::string& name);

}  // namespace lcos

#endif
