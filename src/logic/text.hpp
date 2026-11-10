// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_TEXT_HPP
#define LCOS_LOGIC_TEXT_HPP

#include <cctype>
#include <string>
#include <vector>

namespace lcos {

inline std::string trim_copy(std::string s) {
  while (!s.empty() &&
         std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}

// Keeps empty fields, so "us," and ",nodeadkeys" round-trip.
// An empty input is an empty list, not a single empty field.
inline std::vector<std::string> split_char(const std::string& s, char sep) {
  std::vector<std::string> out;
  if (s.empty()) {
    return out;
  }
  std::string cur;
  for (char c : s) {
    if (c == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

inline std::string join_char(const std::vector<std::string>& parts, char sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      out.push_back(sep);
    }
    out += parts[i];
  }
  return out;
}

}  // namespace lcos

#endif
