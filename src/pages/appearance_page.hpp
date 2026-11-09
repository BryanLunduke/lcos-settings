// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_APPEARANCE_PAGE_HPP
#define LCOS_APPEARANCE_PAGE_HPP

#include "backends/xfconf_store.hpp"
#include "logic/themes.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/fontbutton.h>
#include <gtkmm/spinbutton.h>

namespace lcos {

class AppearancePage : public SettingsPage {
 public:
  explicit AppearancePage(XfconfStore& store);
  Glib::ustring title() const override { return "Appearance"; }
  Glib::ustring summary() const override {
    return "Widget theme, icons, fonts, and the window-manager theme. Changes apply immediately.";
  }

 private:
  void load();
  void fill_themes(Gtk::ComboBoxText& combo, const std::vector<ThemeEntry>& themes,
                   const std::string& current);
  void sync_xfwm_if_needed();

  XfconfStore& store_;
  int freeze_ = 0;
  Gtk::ComboBoxText gtk_theme_;
  Gtk::ComboBoxText icon_theme_;
  Gtk::ComboBoxText xfwm_theme_;
  Gtk::CheckButton sync_themes_;
  Gtk::FontButton font_;
  Gtk::FontButton mono_;
  Gtk::FontButton title_font_;
  Gtk::ComboBoxText title_align_;
  Gtk::ComboBoxText antialias_;
  Gtk::ComboBoxText hinting_;
  Gtk::ComboBoxText rgba_;
  Gtk::CheckButton custom_dpi_;
  Gtk::SpinButton dpi_;
  Gtk::ComboBoxText toolbar_;
  Gtk::CheckButton menu_images_;
  Gtk::CheckButton button_images_;
  Gtk::CheckButton edit_accels_;
  Gtk::CheckButton header_buttons_;
  Gtk::ComboBoxText window_scale_;
};

}  // namespace lcos

#endif
