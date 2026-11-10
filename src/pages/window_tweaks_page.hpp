// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_WINDOW_TWEAKS_PAGE_HPP
#define LCOS_WINDOW_TWEAKS_PAGE_HPP

#include "backends/xfconf_store.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/notebook.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/scale.h>

namespace lcos {

// xfwm4-tweaks-settings 4.20, without the Workspaces tab. LCOS uses one workspace.
class WindowTweaksPage : public SettingsPage {
 public:
  explicit WindowTweaksPage(XfconfStore& store);

  Glib::ustring title() const override { return "Window Manager Tweaks"; }
  Glib::ustring summary() const override {
    return "Cycling, focus, accessibility, placement, and the compositor. Changes apply immediately.";
  }
  void on_page_shown() override;
  int screenshot_count() const override { return 5; }
  void prepare_screenshot(int index) override;
  std::string screenshot_suffix(int index) const override;

 private:
  void load();
  void sync_compositor();
  void sync_maximize();
  void sync_urgent();

  XfconfStore& store_;
  int freeze_ = 0;
  Gtk::Notebook notebook_;

  Gtk::CheckButton cycle_minimum_;
  Gtk::CheckButton cycle_minimized_;
  Gtk::CheckButton cycle_hidden_;
  Gtk::CheckButton cycle_draw_frame_;
  Gtk::CheckButton cycle_raise_;
  Gtk::CheckButton cycle_list_;

  Gtk::CheckButton prevent_stealing_;
  Gtk::CheckButton focus_hint_;
  Gtk::RadioButton activate_bring_;
  Gtk::RadioButton activate_switch_;
  Gtk::RadioButton activate_none_;

  Gtk::ComboBoxText easy_click_;
  Gtk::CheckButton raise_any_;
  Gtk::CheckButton borderless_;
  Gtk::CheckButton titleless_;
  Gtk::CheckButton tile_on_move_;
  Gtk::CheckButton snap_resist_;
  Gtk::CheckButton rollup_;
  Gtk::CheckButton urgent_blink_;
  Gtk::CheckButton urgent_repeat_;

  Gtk::Scale placement_ratio_;
  Gtk::RadioButton place_center_;
  Gtk::RadioButton place_mouse_;

  Gtk::CheckButton compositing_;
  Gtk::CheckButton unredirect_;
  Gtk::CheckButton cycle_preview_;
  Gtk::CheckButton popup_shadow_;
  Gtk::CheckButton dock_shadow_;
  Gtk::CheckButton frame_shadow_;
  Gtk::CheckButton zoom_desktop_;
  Gtk::CheckButton zoom_pointer_;
  Gtk::Scale frame_opacity_;
  Gtk::Scale inactive_opacity_;
  Gtk::Scale move_opacity_;
  Gtk::Scale resize_opacity_;
  Gtk::Scale popup_opacity_;
  Gtk::Box* compositor_options_ = nullptr;
};

}  // namespace lcos

#endif
