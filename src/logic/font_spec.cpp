// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/font_spec.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace lcos {

FontSpec parse_font_spec(const std::string& spec) {
  FontSpec out;
  out.family = spec;
  const auto end = spec.find_last_not_of(" \t");
  if (end == std::string::npos) {
    out.family.clear();
    return out;
  }
  const std::string text = spec.substr(0, end + 1);
  const auto space = text.rfind(' ');
  if (space == std::string::npos || space == 0) {
    return out;
  }
  const std::string tail = text.substr(space + 1);
  try {
    std::size_t used = 0;
    const double size = std::stod(tail, &used);
    if (used != tail.size() || !std::isfinite(size) || size < 1.0 || size > 128.0) {
      return out;
    }
    std::string family = text.substr(0, space);
    while (!family.empty() && (family.back() == ' ' || family.back() == ',')) {
      family.pop_back();
    }
    if (family.empty()) {
      return out;
    }
    out.family = family;
    out.size = size;
    out.parsed = true;
  } catch (const std::exception&) {
  }
  return out;
}

std::string format_font_spec(const std::string& family, double size) {
  std::ostringstream number;
  if (!std::isfinite(size)) {
    size = 10;
  }
  if (std::fabs(size - std::round(size)) < 0.001) {
    number << static_cast<int>(std::lround(size));
  } else {
    number << std::setprecision(4) << std::defaultfloat << size;
  }
  if (family.empty()) {
    return number.str();
  }
  return family + " " + number.str();
}

}  // namespace lcos
