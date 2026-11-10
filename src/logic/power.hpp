// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_POWER_HPP
#define LCOS_LOGIC_POWER_HPP

#include <string>

namespace lcos {

// /sys/class/power_supply/*/type, including a trailing newline.
bool power_supply_type_is_battery(const std::string& type_file);

// xfce4-power-manager stores the dim timeout as MAX(9, 10 * slider).
// Slider 0 is "Never" and is stored as the minimum, 9 seconds.
unsigned brightness_delay_from_slider(int slider);
int brightness_slider_from_delay(unsigned stored);

}  // namespace lcos

#endif
