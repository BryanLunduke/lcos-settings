// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_MOUSE_PAGE_HPP
#define LCOS_MOUSE_PAGE_HPP

#include "backends/pointer_backend.hpp"
#include "backends/xfconf_store.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/scale.h>
#include <gtkmm/spinbutton.h>

#include <vector>

namespace lcos {

class MousePage : public SettingsPage {
 public:
  MousePage(XfconfStore& store, PointerBackend& pointers);
  Glib::ustring title() const override { return "Mouse"; }
  Glib::ustring summary() const override {
    return "Buttons, speed, touchpad, and the cursor theme. Changes apply immediately.";
  }

 private:
  void reload_devices();
  void show_device();
  const PointerDevice* current() const;
  void write_bool_atom(const char* atom, bool value);
  void write_scroll_method();

  XfconfStore& store_;
  PointerBackend& pointers_;
  std::vector<PointerDevice> devices_;
  int freeze_ = 0;

  Gtk::ComboBoxText device_;
  Gtk::CheckButton enabled_;
  Gtk::RadioButton right_handed_;
  Gtk::RadioButton left_handed_;
  Gtk::Scale acceleration_;
  Gtk::Scale threshold_;
  Gtk::CheckButton reverse_;
  Gtk::CheckButton tap_;
  Gtk::CheckButton natural_;
  Gtk::CheckButton disable_typing_;
  Gtk::CheckButton disable_typing_global_;
  Gtk::SpinButton typing_duration_;
  Gtk::ComboBoxText scroll_method_;
  Gtk::ComboBoxText click_method_;
  Gtk::CheckButton middle_;
  Gtk::SpinButton double_time_;
  Gtk::SpinButton double_distance_;
  Gtk::SpinButton drag_;
  Gtk::ComboBoxText cursor_theme_;
  Gtk::SpinButton cursor_size_;
  Gtk::Label* device_note_ = nullptr;
};

}  // namespace lcos

#endif
