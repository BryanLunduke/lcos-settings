// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_BUTTON_LAYOUT_HPP
#define LCOS_LOGIC_BUTTON_LAYOUT_HPP

#include <string>

namespace lcos {

// xfwm4 /general/button_layout. Letters before '|' sit on the left of the
// title, letters after it sit on the right.
// O menu, T stick, H hide, S shade, M maximize, C close.
inline constexpr const char* kTitleButtonLetters = "OTHSMC";
inline constexpr const char* kDefaultButtonLayout = "O|SHMC";

struct TitleBarButtons {
  std::string left;
  std::string right;
};

bool title_button_known(char code);
const char* title_button_label(char code);

// Drops unknown letters and duplicates. A missing bar keeps every button on
// the left of the title, matching xfwm4 when the separator is absent.
TitleBarButtons parse_button_layout(const std::string& layout);
std::string format_button_layout(const TitleBarButtons& buttons);

// Letters from kTitleButtonLetters that are on neither side, in that order.
std::string hidden_title_buttons(const TitleBarButtons& buttons);

// action is "up", "down", "left", "right", or "hide".
// up/down reorder inside the side that currently holds the button.
// left/right move it to the end of that side. hide takes it off the title.
TitleBarButtons adjust_button_layout(TitleBarButtons buttons, char code, const char* action);

}  // namespace lcos

#endif
