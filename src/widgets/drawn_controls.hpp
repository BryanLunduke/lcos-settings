// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_DRAWN_CONTROLS_HPP
#define LCOS_DRAWN_CONTROLS_HPP

namespace lcos {

// Draws switches and combo boxes itself so they match the Command Center
// mockups on the Clearlooks theme, which ignores most of the CSS for those
// widgets. The widgets stay the real GTK switch and combo, so keyboard
// operation and the accessibility tree do not change.
void install_drawn_controls();

}  // namespace lcos

#endif
