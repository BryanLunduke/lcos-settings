// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/power.hpp"

#include <algorithm>
#include <cctype>

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

}  // namespace lcos
