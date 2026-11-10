// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_DESKTOP_PAGE_HPP
#define LCOS_DESKTOP_PAGE_HPP

#include "backends/xfconf_store.hpp"
#include "backends/xrandr_backend.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/checkbutton.h>
#include <gtkmm/colorbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/filechooserbutton.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <string>
#include <vector>

namespace lcos {

class DesktopPage : public SettingsPage {
 public:
  DesktopPage(XfconfStore& store, XrandrBackend& randr);

  Glib::ustring title() const override { return "Desktop"; }
  Glib::ustring summary() const override {
    return "Wallpaper for each monitor and workspace. This is the Background tab from xfdesktop.";
  }
  void on_page_shown() override;

 private:
  void reload_monitors();
  void reload_fields();
  void scan_folder();
  void choose_image(const std::string& path);
  std::string property(const std::string& leaf) const;
  int workspace_index() const;
  void write_uint(const char* leaf, unsigned value);
  void write_bool(const char* leaf, bool value);

  XfconfStore& store_;
  XrandrBackend& randr_;
  int freeze_ = 0;
  std::vector<std::string> image_paths_;

  Gtk::ComboBoxText monitor_;
  Gtk::SpinButton workspace_;
  Gtk::CheckButton all_workspaces_;
  Gtk::FileChooserButton folder_;
  Gtk::FlowBox images_;
  Gtk::Label* image_caption_ = nullptr;
  Gtk::ComboBoxText style_;
  Gtk::ComboBoxText color_style_;
  Gtk::ColorButton color1_;
  Gtk::ColorButton color2_;
  Gtk::CheckButton cycle_;
  Gtk::ComboBoxText cycle_period_;
  Gtk::SpinButton cycle_timer_;
  Gtk::CheckButton cycle_random_;
};

}  // namespace lcos

#endif
