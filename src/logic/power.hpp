// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_POWER_HPP
#define LCOS_LOGIC_POWER_HPP

#include <cstdint>
#include <string>

namespace lcos {

// /sys/class/power_supply/*/type, including a trailing newline.
bool power_supply_type_is_battery(const std::string& type_file);

// xfce4-power-manager stores the dim timeout as MAX(9, 10 * slider).
// Slider 0 is "Never" and is stored as the minimum, 9 seconds.
unsigned brightness_delay_from_slider(int slider);
int brightness_slider_from_delay(unsigned stored);

// True when the directory contains at least one backlight device name.
bool backlight_directory_has_device(const std::string& sys_class_backlight);

std::string upower_kind_name(unsigned kind);
std::string upower_state_name(unsigned state);
std::string upower_technology_name(unsigned technology);
// UPower times are seconds. Zero and negative mean unknown.
std::string format_upower_duration(std::int64_t seconds);
std::string format_upower_measure(double value, const char* unit);

}  // namespace lcos

#endif
