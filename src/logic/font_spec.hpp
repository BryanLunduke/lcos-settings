// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_FONT_SPEC_HPP
#define LCOS_LOGIC_FONT_SPEC_HPP

#include <string>

namespace lcos {

// A Pango font description as GTK font buttons store it: "Family 10" or
// "Family 10.5". The size is the trailing number, so "Sans Bold 9" keeps
// "Sans Bold" as the family.
struct FontSpec {
  std::string family;
  double size = 10;
  bool parsed = false;
};

FontSpec parse_font_spec(const std::string& spec);
std::string format_font_spec(const std::string& family, double size);

}  // namespace lcos

#endif
