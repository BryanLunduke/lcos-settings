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

// Shape of the wallpaper monitor preview. The real width/height ratio is
// used when it sits between 4:3 and 16:9, inclusive. Wider, taller, or
// unknown screens use 16:10 so the drawing stays a normal monitor.
double preview_monitor_aspect(int width, int height);

}  // namespace lcos

#endif
