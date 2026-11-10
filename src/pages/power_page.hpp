// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_POWER_PAGE_HPP
#define LCOS_POWER_PAGE_HPP

#include "backends/upower_client.hpp"
#include "backends/xfconf_store.hpp"
#include "backends/xrandr_backend.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/notebook.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/stack.h>

#include <string>

namespace lcos {

class PowerPage : public SettingsPage {
 public:
  PowerPage(XfconfStore& store, XrandrBackend& randr);

  Glib::ustring title() const override { return "Power"; }
  Glib::ustring summary() const override {
    return "Buttons, sleep, display blanking, and lock. Values are written only after you change them.";
  }
  int screenshot_count() const override { return 4; }
  void prepare_screenshot(int index) override;
  std::string screenshot_suffix(int index) const override;

 private:
  Gtk::Widget* scrolled(Gtk::Widget& child);
  void bind_uint(Gtk::ComboBoxText& combo, const char* leaf, unsigned fallback);
  void bind_minutes(Gtk::SpinButton& spin, const char* leaf, unsigned fallback);
  void bind_bool(Gtk::CheckButton& check, const char* leaf, bool fallback);
  void bind_string(Gtk::ComboBoxText& combo, const char* leaf, const std::string& fallback);
  void bind_brightness_delay(Gtk::SpinButton& spin, const char* leaf, unsigned fallback);
  std::string prop(const char* leaf) const;
  void refresh_devices();
  void show_device(const std::string& path);

  XfconfStore& store_;
  UpowerClient upower_;
  Gtk::Notebook notebook_;
  Gtk::ListBox device_list_;
  Gtk::Grid detail_grid_;
  Gtk::Label device_empty_;
  Gtk::Stack device_face_;
  std::string selected_device_;
  int freeze_ = 0;
};

}  // namespace lcos

#endif
