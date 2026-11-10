// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/power.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include <glib.h>

namespace lcos {

bool power_supply_type_is_battery(const std::string& type_file) {
  std::string text = type_file;
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.pop_back();
  }
  std::size_t start = 0;
  while (start < text.size() &&
         std::isspace(static_cast<unsigned char>(text[start])) != 0) {
    ++start;
  }
  return text.substr(start) == "Battery";
}

unsigned brightness_delay_from_slider(int slider) {
  if (slider < 1) {
    return 9;
  }
  return std::max(9u, static_cast<unsigned>(slider) * 10u);
}

int brightness_slider_from_delay(unsigned stored) {
  if (stored <= 9) {
    return 0;
  }
  return static_cast<int>(stored / 10u);
}

bool backlight_directory_has_device(const std::string& sys_class_backlight) {
  GDir* dir = g_dir_open(sys_class_backlight.c_str(), 0, nullptr);
  if (dir == nullptr) {
    return false;
  }
  const bool any = g_dir_read_name(dir) != nullptr;
  g_dir_close(dir);
  return any;
}

std::string upower_kind_name(unsigned kind) {
  switch (kind) {
    case 1:
      return "Line power";
    case 2:
      return "Battery";
    case 3:
      return "UPS";
    case 4:
      return "Monitor";
    case 5:
      return "Mouse";
    case 6:
      return "Keyboard";
    case 7:
      return "PDA";
    case 8:
      return "Phone";
    case 9:
      return "Media player";
    case 10:
      return "Tablet";
    case 11:
      return "Computer";
    case 12:
      return "Gaming input";
    case 13:
      return "Pen";
    default:
      return "Unknown";
  }
}

std::string upower_state_name(unsigned state) {
  switch (state) {
    case 1:
      return "Charging";
    case 2:
      return "Discharging";
    case 3:
      return "Empty";
    case 4:
      return "Fully charged";
    case 5:
      return "Pending charge";
    case 6:
      return "Pending discharge";
    default:
      return "Unknown";
  }
}

std::string upower_technology_name(unsigned technology) {
  switch (technology) {
    case 1:
      return "Lithium ion";
    case 2:
      return "Lithium polymer";
    case 3:
      return "Lithium iron phosphate";
    case 4:
      return "Lead acid";
    case 5:
      return "Nickel cadmium";
    case 6:
      return "Nickel metal hydride";
    default:
      return "Unknown";
  }
}

std::string format_upower_duration(std::int64_t seconds) {
  if (seconds <= 0) {
    return {};
  }
  const auto hours = seconds / 3600;
  const auto minutes = (seconds % 3600) / 60;
  if (hours > 0) {
    return std::to_string(hours) + " h " + std::to_string(minutes) + " min";
  }
  if (minutes > 0) {
    return std::to_string(minutes) + " min";
  }
  return std::to_string(seconds) + " s";
}

std::string format_upower_measure(double value, const char* unit) {
  if (!(value > 0.0) || unit == nullptr) {
    return {};
  }
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "%.2f %s", value, unit);
  return buffer;
}

}  // namespace lcos
