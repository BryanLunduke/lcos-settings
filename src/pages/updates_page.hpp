// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_UPDATES_PAGE_HPP
#define LCOS_UPDATES_PAGE_HPP

#include "pages/settings_page.hpp"

#include <gtkmm/button.h>
#include <gtkmm/label.h>

namespace lcos {

class UpdatesPage : public SettingsPage {
 public:
  UpdatesPage();
  Glib::ustring title() const override { return "Updates"; }
  Glib::ustring summary() const override { return "Check for updates with lcos-updates."; }
  void on_page_shown() override { refresh(); }

 private:
  void refresh();

  Gtk::Label* status_ = nullptr;
  Gtk::Label* checked_ = nullptr;
  Gtk::Label* available_ = nullptr;
  Gtk::Button check_;
};

}  // namespace lcos

#endif
