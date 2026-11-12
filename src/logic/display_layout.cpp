// SPDX-License-Identifier: GPL-3.0-or-later

#include "display_layout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace lcos {

bool parse_resolution(const std::string& text, int& width, int& height) {
  const auto x = text.find('x');
  if (x == std::string::npos || x == 0 || x + 1 >= text.size()) {
    return false;
  }
  try {
    std::size_t used_w = 0;
    std::size_t used_h = 0;
    const int w = std::stoi(text.substr(0, x), &used_w);
    const int h = std::stoi(text.substr(x + 1), &used_h);
    if (used_w != x || used_h == 0 || w <= 0 || h <= 0) {
      return false;
    }
    // Allow a trailing marker such as "1080i" only when the digits parse.
    width = w;
    height = h;
    return true;
  } catch (...) {
    return false;
  }
}

std::string format_resolution(int width, int height) {
  return std::to_string(width) + "x" + std::to_string(height);
}

std::string aspect_ratio_text(int width, int height) {
  if (width <= 0 || height <= 0) {
    return {};
  }
  const double ratio = static_cast<double>(width) / static_cast<double>(height);
  struct Known {
    int w;
    int h;
    const char* label;
  };
  const Known known[] = {{16, 10, "16:10"}, {8, 5, "16:10"}, {16, 9, "16:9"}, {4, 3, "4:3"},
                         {5, 4, "5:4"},     {3, 2, "3:2"},   {21, 9, "21:9"}, {32, 9, "32:9"},
                         {1, 1, "1:1"},     {5, 3, "5:3"},   {2, 1, "2:1"}};
  for (const auto& item : known) {
    if (std::abs(ratio - static_cast<double>(item.w) / static_cast<double>(item.h)) < 0.02) {
      return item.label;
    }
  }
  int a = width;
  int b = height;
  while (b != 0) {
    const int t = a % b;
    a = b;
    b = t;
  }
  if (a <= 0) {
    return {};
  }
  return std::to_string(width / a) + ":" + std::to_string(height / a);
}

bool refresh_matches(double a, double b) {
  return std::llround(a * 100.0) == std::llround(b * 100.0);
}

double mode_refresh_hz(unsigned long dot_clock, unsigned int h_total,
                       unsigned int v_total) {
  if (h_total == 0 || v_total == 0) {
    return 0;
  }
  return static_cast<double>(dot_clock) /
         (static_cast<double>(h_total) * static_cast<double>(v_total));
}

int rotated_width(const OutputConfig& output) {
  if (output.rotation == 90 || output.rotation == 270) {
    return output.height;
  }
  return output.width;
}

int rotated_height(const OutputConfig& output) {
  if (output.rotation == 90 || output.rotation == 270) {
    return output.width;
  }
  return output.height;
}

double ui_scale(const OutputConfig& output) {
  return output.scale > 0.05 ? output.scale : 1.0;
}

double xfconf_scale_from_ui(double ui) {
  const double safe = ui > 0.05 ? ui : 1.0;
  return 1.0 / safe;
}

double ui_scale_from_xfconf(double stored) {
  if (!(stored > 0.0)) {
    return 1.0;
  }
  return 1.0 / stored;
}

int footprint_width(const OutputConfig& output) {
  // XFCE 4.20 stores the RandR transform as 1/ui-scale. The framebuffer
  // footprint is the mode size divided by that ui scale.
  const double scaled = rotated_width(output) / ui_scale(output);
  return std::max(1, static_cast<int>(std::lround(scaled)));
}

int footprint_height(const OutputConfig& output) {
  const double scaled = rotated_height(output) / ui_scale(output);
  return std::max(1, static_cast<int>(std::lround(scaled)));
}

ScreenBounds screen_bounds(const std::vector<OutputConfig>& outputs) {
  ScreenBounds bounds;
  bool any = false;
  for (const auto& output : outputs) {
    if (!output.active) {
      continue;
    }
    any = true;
    bounds.width = std::max(bounds.width, output.x + footprint_width(output));
    bounds.height = std::max(bounds.height, output.y + footprint_height(output));
  }
  if (!any) {
    bounds.width = 1;
    bounds.height = 1;
  }
  return bounds;
}

void normalize_positions(std::vector<OutputConfig>& outputs) {
  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  bool any = false;
  for (const auto& output : outputs) {
    if (!output.active) {
      continue;
    }
    any = true;
    min_x = std::min(min_x, output.x);
    min_y = std::min(min_y, output.y);
  }
  if (!any || (min_x == 0 && min_y == 0)) {
    return;
  }
  for (auto& output : outputs) {
    if (!output.active) {
      continue;
    }
    output.x -= min_x;
    output.y -= min_y;
  }
}

void arrange_extend(std::vector<OutputConfig>& outputs) {
  int x = 0;
  for (auto& output : outputs) {
    if (!output.active) {
      continue;
    }
    output.x = x;
    output.y = 0;
    x += footprint_width(output);
  }
}

namespace {

bool has_size(const OutputConfig& output, int width, int height) {
  for (const auto& mode : output.modes) {
    if (mode.width == width && mode.height == height) {
      return true;
    }
  }
  return false;
}

}  // namespace

double closest_refresh(const OutputConfig& output, int width, int height,
                       double preferred) {
  const ModeInfo* best = nullptr;
  double best_distance = 0;
  for (const auto& mode : output.modes) {
    if (mode.width != width || mode.height != height) {
      continue;
    }
    const double distance = std::fabs(mode.refresh - preferred);
    if (best == nullptr || distance < best_distance) {
      best = &mode;
      best_distance = distance;
    }
  }
  return best != nullptr ? best->refresh : preferred;
}

void arrange_mirror(std::vector<OutputConfig>& outputs) {
  std::vector<OutputConfig*> active;
  for (auto& output : outputs) {
    if (output.active) {
      active.push_back(&output);
    }
  }
  if (active.empty()) {
    return;
  }

  ModeInfo best;
  bool found = false;
  for (const auto& mode : active.front()->modes) {
    bool common = true;
    for (std::size_t i = 1; i < active.size(); ++i) {
      if (!has_size(*active[i], mode.width, mode.height)) {
        common = false;
        break;
      }
    }
    if (!common) {
      continue;
    }
    if (!found || mode.width * mode.height > best.width * best.height) {
      best = mode;
      found = true;
    }
  }

  for (auto* output : active) {
    output->x = 0;
    output->y = 0;
    if (!found) {
      continue;
    }
    output->width = best.width;
    output->height = best.height;
    output->refresh =
        closest_refresh(*output, best.width, best.height, best.refresh);
  }
}

const ModeInfo* find_mode(const std::vector<ModeInfo>& modes, int width,
                          int height, double refresh) {
  const ModeInfo* fallback = nullptr;
  for (const auto& mode : modes) {
    if (mode.width != width || mode.height != height) {
      continue;
    }
    if (fallback == nullptr || mode.refresh > fallback->refresh) {
      fallback = &mode;
    }
    if (refresh > 0 && refresh_matches(mode.refresh, refresh)) {
      return &mode;
    }
  }
  if (refresh <= 0) {
    return fallback;
  }
  return nullptr;
}

std::string friendly_output_name(const std::string& randr_name,
                                 const std::string& edid_name) {
  if (!edid_name.empty()) {
    return edid_name;
  }
  if (randr_name.rfind("eDP", 0) == 0 || randr_name.rfind("LVDS", 0) == 0 ||
      randr_name.rfind("DSI", 0) == 0 || randr_name == "PANEL") {
    return "Laptop";
  }
  return randr_name;
}

int millimeters_for_pixels(int pixels) {
  if (pixels < 1) {
    pixels = 1;
  }
  return std::max(1, static_cast<int>(std::lround((pixels / 96.0) * 25.4)));
}

bool crtc_fits_framebuffer(int x, int y, int fb_width, int fb_height, int screen_width,
                           int screen_height) {
  if (fb_width <= 0 || fb_height <= 0) {
    return true;
  }
  if (screen_width <= 0 || screen_height <= 0) {
    return false;
  }
  return x >= 0 && y >= 0 && x <= screen_width - fb_width && y <= screen_height - fb_height;
}

namespace {

std::string reflection_key(const std::string& value) {
  return value.empty() || value == "0" ? std::string("0") : value;
}

const OutputConfig* output_named(const std::vector<OutputConfig>& outputs, const std::string& name) {
  for (const auto& output : outputs) {
    if (output.name == name) {
      return &output;
    }
  }
  return nullptr;
}

bool output_geometry_matches(const OutputConfig& left, const OutputConfig& right) {
  if (left.active != right.active || left.primary != right.primary) {
    return false;
  }
  if (!left.active) {
    return true;
  }
  if (left.x != right.x || left.y != right.y || left.width != right.width ||
      left.height != right.height || left.rotation != right.rotation) {
    return false;
  }
  if (reflection_key(left.reflection) != reflection_key(right.reflection)) {
    return false;
  }
  const double left_scale = left.scale > 0.05 ? left.scale : 1.0;
  const double right_scale = right.scale > 0.05 ? right.scale : 1.0;
  return std::abs(left_scale - right_scale) <= 0.02;
}

}  // namespace

bool layouts_equivalent(const std::vector<OutputConfig>& left, const std::vector<OutputConfig>& right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (const auto& output : left) {
    const OutputConfig* match = output_named(right, output.name);
    if (match == nullptr || !output_geometry_matches(output, *match)) {
      return false;
    }
  }
  return true;
}

namespace {

std::vector<std::string> path_parts(const std::string& path) {
  std::vector<std::string> parts;
  std::string part;
  for (char c : path) {
    if (c == '/') {
      if (!part.empty()) {
        parts.push_back(part);
        part.clear();
      }
    } else {
      part.push_back(c);
    }
  }
  if (!part.empty()) {
    parts.push_back(part);
  }
  return parts;
}

const std::string* property_value(
    const std::vector<std::pair<std::string, std::string>>& properties, const std::string& path) {
  for (const auto& entry : properties) {
    if (entry.first == path) {
      return &entry.second;
    }
  }
  return nullptr;
}

}  // namespace

bool is_user_display_profile(
    const std::string& profile,
    const std::vector<std::pair<std::string, std::string>>& properties) {
  if (profile.empty() || profile.find('/') != std::string::npos) {
    return false;
  }
  for (const auto& entry : properties) {
    const auto parts = path_parts(entry.first);
    // /Profile/Output is the output node xfce writes the friendly name into.
    if (parts.size() != 2 || parts[0] != profile) {
      continue;
    }
    const std::string* edid =
        property_value(properties, "/" + profile + "/" + parts[1] + "/EDID");
    if (edid != nullptr && !edid->empty()) {
      return true;
    }
  }
  return false;
}

bool display_profile_matches(
    const std::string& profile, const std::vector<std::string>& connected_edids,
    const std::vector<std::pair<std::string, std::string>>& properties) {
  if (!is_user_display_profile(profile, properties)) {
    return false;
  }
  std::vector<std::string> stored;
  for (const auto& entry : properties) {
    const auto parts = path_parts(entry.first);
    if (parts.size() != 2 || parts[0] != profile) {
      continue;
    }
    const std::string* edid =
        property_value(properties, "/" + profile + "/" + parts[1] + "/EDID");
    if (edid == nullptr || edid->empty()) {
      continue;
    }
    stored.push_back(*edid);
    if (stored.size() > connected_edids.size()) {
      return false;
    }
    bool found = false;
    for (const auto& live : connected_edids) {
      if (live == *edid) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return stored.size() == connected_edids.size();
}

std::vector<std::string> matching_display_profiles(
    const std::vector<std::string>& connected_edids,
    const std::vector<std::pair<std::string, std::string>>& properties) {
  std::set<std::string> ids;
  for (const auto& entry : properties) {
    const auto parts = path_parts(entry.first);
    if (!parts.empty()) {
      ids.insert(parts[0]);
    }
  }
  std::vector<std::string> matched;
  for (const auto& id : ids) {
    if (id == "Temp") {
      continue;
    }
    if (display_profile_matches(id, connected_edids, properties)) {
      matched.push_back(id);
    }
  }
  return matched;
}

}  // namespace lcos
