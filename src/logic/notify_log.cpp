// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/notify_log.hpp"

#include "logic/text.hpp"

#include <sstream>

namespace lcos {

int notify_log_entry_count(const std::string& text) {
  int count = 0;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    line = trim_copy(line);
    if (line.size() < 3 || line.front() != '[' || line.back() != ']') {
      continue;
    }
    const std::string name = trim_copy(line.substr(1, line.size() - 2));
    if (!name.empty() && name.find('[') == std::string::npos && name.find(']') == std::string::npos) {
      ++count;
    }
  }
  return count;
}

}  // namespace lcos
