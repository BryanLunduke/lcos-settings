// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_DISPLAY_MINIMAL_HPP
#define LCOS_DISPLAY_MINIMAL_HPP

#include "backends/xfconf_store.hpp"
#include "backends/xrandr_backend.hpp"

#include <gtkmm/window.h>

#include <functional>

namespace lcos {

// The hot-plug dialog xfsettingsd used to open with
// `xfce4-display-settings --minimal`. Mirror or extend, or open the full
// Display page.
class DisplayMinimal : public Gtk::Window {
 public:
  DisplayMinimal(XfconfStore& store, XrandrBackend& randr, std::function<void()> open_full);

 private:
  void apply_layout(bool mirror);

  XfconfStore& store_;
  XrandrBackend& randr_;
  std::function<void()> open_full_;
};

}  // namespace lcos

#endif
