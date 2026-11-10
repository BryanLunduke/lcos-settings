// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LANGUAGE_PAGE_HPP
#define LCOS_LANGUAGE_PAGE_HPP

#include "backends/priv_client.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/comboboxtext.h>

#include <string>
#include <vector>

namespace lcos {

class LanguagePage : public SettingsPage {
 public:
  LanguagePage(PrivSession& session, Gtk::Window& parent);

  Glib::ustring title() const override { return "Language"; }
  Glib::ustring summary() const override {
    return "System language and regional formats. Keyboard layouts stay on the Keyboard page.";
  }
  bool has_pending() const override { return session_.dirty(); }
  bool commit_pending() override;
  void revert_page() override;
  void on_page_shown() override;

 private:
  void load();
  void apply();
  void tell(const std::string& message);
  void fill(Gtk::ComboBoxText& combo, bool same_as_language);

  PrivSession& session_;
  Gtk::Window& parent_;
  int freeze_ = 0;
  std::vector<std::string> locales_;
  Gtk::ComboBoxText language_;
  Gtk::ComboBoxText dates_;
  Gtk::ComboBoxText numbers_;
  Gtk::ComboBoxText currency_;
};

}  // namespace lcos

#endif
