// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_BACKDROP_HPP
#define LCOS_LOGIC_BACKDROP_HPP

#include <string>

namespace lcos {

// xfdesktop removes every space and tab from the property path, including
// the monitor name. Screen is 0 on a single X screen.
std::string backdrop_property(int screen, const std::string& monitor, int workspace,
                              const std::string& leaf);

bool is_backdrop_image(const std::string& filename);

}  // namespace lcos

#endif
