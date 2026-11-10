// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_NAMES_HPP
#define LCOS_LOGIC_NAMES_HPP

#include <string>

namespace lcos {

// xfce4-settings xfce_pointers_helper_device_xfconf_name:
// keep A-Z a-z 0-9 _ -, turn spaces into _, drop every other character.
std::string device_xfconf_name(const std::string& raw);

// Spaces become underscores. Used for XInput property atoms stored under
// /<device>/Properties/<name>.
std::string property_xfconf_token(const std::string& atom_name);

std::string pointer_prop(const std::string& device_xfconf, const std::string& leaf);

std::string pointer_xinput_prop(const std::string& device_xfconf,
                                const std::string& atom_name);

// /<scheme>/<output> or /<scheme>/<output>/<leaf>
std::string display_prop(const std::string& scheme, const std::string& output,
                         const std::string& leaf);

// RandR names are usually already safe. Anything else is rewritten so the
// string can sit in an xfconf path.
std::string output_xfconf_name(const std::string& randr_name);

std::string shortcut_prop(const std::string& base, const std::string& accel);

// Profile ids are a single xfconf path component.
std::string profile_id_from_label(const std::string& label);

double acceleration_to_libinput(double acceleration);
double libinput_to_acceleration(double libinput_speed);

}  // namespace lcos

#endif
