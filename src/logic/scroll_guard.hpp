// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_SCROLL_GUARD_HPP
#define LCOS_LOGIC_SCROLL_GUARD_HPP

namespace lcos {

// A wheel over a combo, slider, or spin changes that control only when the
// control (or a child, such as the combo's entry) has focus. Otherwise the
// page scrolls. gtkmm's signal connect defaults to after=true, which is too
// late: the widget has already applied the wheel. The guard connects first.
inline bool scroll_event_should_block_change(bool widget_or_child_focused) {
  return !widget_or_child_focused;
}

inline bool scroll_guard_connects_after_default() { return false; }

}  // namespace lcos

#endif
