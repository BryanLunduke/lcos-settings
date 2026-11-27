// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_UPDATES_STATUS_HPP
#define LCOS_LOGIC_UPDATES_STATUS_HPP

#include <string>

namespace lcos {

// lcos-updates does not publish a D-Bus API. A status file, when one has
// been written, is two lines:
//   last-check: <unix seconds>
//   available: <count>
struct UpdateSnapshot {
  bool present = false;
  long long last_check = -1;
  int available = -1;
};

UpdateSnapshot parse_update_status(const std::string& text);
std::string update_status_path(const std::string& cache_home);

}  // namespace lcos

#endif
