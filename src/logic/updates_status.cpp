// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/updates_status.hpp"

#include <cctype>
#include <sstream>

namespace lcos {
namespace {

std::string trim(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return text.substr(begin, end - begin);
}

}  // namespace

UpdateSnapshot parse_update_status(const std::string& text) {
  UpdateSnapshot snapshot;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    const std::string key = trim(line.substr(0, colon));
    const std::string value = trim(line.substr(colon + 1));
    try {
      if (key == "last-check" || key == "last_check") {
        snapshot.last_check = std::stoll(value);
        snapshot.present = true;
      } else if (key == "available" || key == "count") {
        snapshot.available = std::stoi(value);
        if (snapshot.available < 0) {
          snapshot.available = -1;
        } else {
          snapshot.present = true;
        }
      }
    } catch (const std::exception&) {
    }
  }
  return snapshot;
}

std::string update_status_path(const std::string& cache_home) {
  if (cache_home.empty()) {
    return {};
  }
  return cache_home + "/lcos-updates/status";
}

}  // namespace lcos
