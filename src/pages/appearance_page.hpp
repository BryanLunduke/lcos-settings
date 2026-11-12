// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_APPEARANCE_PAGE_HPP
#define LCOS_APPEARANCE_PAGE_HPP

#include "backends/xfconf_store.hpp"
#include "logic/themes.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/dialog.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/fontbutton.h>
#include <gtkmm/spinbutton.h>

#include <glibmm/main.h>

namespace lcos {

class AppearancePage : public SettingsPage {
 public:
  explicit AppearancePage(XfconfStore& store);
  ~AppearancePage() override;
  Glib::ustring title() const override { return "Appearance"; }
  Glib::ustring summary() const override {
    return "Style, icons, and fonts for windows and applications.";
  }
  bool dark_style_available() const;
  bool dark_style_on() const;
  void set_dark_style(bool on);
  void reload_from_store() { load(); }

 private:
  void load();
  void fill_themes(Gtk::ComboBoxText& combo, const std::vector<ThemeEntry>& themes,
                   const std::string& current);
  void sync_xfwm_if_needed();
  void rebuild_cards();
  void schedule_cards();
  void show_font_dialog();
  void set_default_family(const std::string& family);
  void set_default_size(int size);

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
  Gtk::FlowBox style_cards_;
  Gtk::FlowBox icon_cards_;
  Gtk::FlowBox font_cards_;
  Gtk::Box size_buttons_;
  Gtk::Button* font_size_buttons_[4] = {};
  std::vector<ThemeEntry> gtk_themes_;
  std::vector<ThemeEntry> icon_themes_;
  std::string light_theme_;
  Gtk::Dialog* fonts_dialog_ = nullptr;
  sigc::connection cards_idle_;
  int shown_font_size_ = 10;
};

}  // namespace lcos

#endif
