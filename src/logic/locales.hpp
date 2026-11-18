// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_LOCALES_HPP
#define LCOS_LOGIC_LOCALES_HPP

#include <string>

namespace lcos {

// "de_DE.UTF-8" -> "German (Germany)". Unknown codes come back unchanged.
std::string locale_display_name(const std::string& code);

// A short date, number, and currency sample in `code`, or empty when that
// locale is not generated on this system.
std::string locale_sample(const std::string& code);

}  // namespace lcos

#endif
