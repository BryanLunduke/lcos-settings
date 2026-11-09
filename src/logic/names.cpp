// SPDX-License-Identifier: GPL-3.0-or-later

#include "names.hpp"

#include <algorithm>
#include <cmath>

namespace lcos {
namespace {

bool is_safe_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '-';
}

}  // namespace

std::string device_xfconf_name(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    if (is_safe_char(c)) {
      out.push_back(c);
    } else if (c == ' ') {
      out.push_back('_');
    }
  }
  return out;
}

std::string property_xfconf_token(const std::string& atom_name) {
  std::string out = atom_name;
  for (char& c : out) {
    if (c == ' ') {
      c = '_';
    }
  }
  return out;
}

std::string pointer_prop(const std::string& device_xfconf, const std::string& leaf) {
  return "/" + device_xfconf + "/" + leaf;
}

std::string pointer_xinput_prop(const std::string& device_xfconf,
                                const std::string& atom_name) {
  return "/" + device_xfconf + "/Properties/" + property_xfconf_token(atom_name);
}

std::string display_prop(const std::string& scheme, const std::string& output,
                         const std::string& leaf) {
  std::string path = "/" + scheme + "/" + output;
  if (!leaf.empty()) {
    path.push_back('/');
    path += leaf;
  }
  return path;
}

std::string output_xfconf_name(const std::string& randr_name) {
  std::string out;
  out.reserve(randr_name.size());
  for (char c : randr_name) {
    if (is_safe_char(c)) {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  if (out.empty()) {
    out = "Output";
  }
  return out;
}

std::string shortcut_prop(const std::string& base, const std::string& accel) {
  std::string path = base;
  if (path.empty() || path.back() != '/') {
    path.push_back('/');
  }
  path += accel;
  return path;
}

std::string profile_id_from_label(const std::string& label) {
  std::string out = "P_";
  for (char c : label) {
    if (is_safe_char(c)) {
      out.push_back(c);
    } else if (c == ' ') {
      out.push_back('_');
    }
  }
  if (out.size() == 2) {
    out += "profile";
  }
  return out;
}

double acceleration_to_libinput(double acceleration) {
  const double speed = (acceleration / 5.0) - 1.0;
  return std::clamp(speed, -1.0, 1.0);
}

double libinput_to_acceleration(double libinput_speed) {
  const double clamped = std::clamp(libinput_speed, -1.0, 1.0);
  return (clamped + 1.0) * 5.0;
}

}  // namespace lcos
