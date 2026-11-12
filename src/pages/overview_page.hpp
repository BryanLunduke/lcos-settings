// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_OVERVIEW_PAGE_HPP
#define LCOS_OVERVIEW_PAGE_HPP

#include "backends/pulse_client.hpp"
#include "backends/xfconf_store.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/scale.h>
#include <gtkmm/switch.h>

#include <functional>
#include <string>

namespace lcos {

class AppearancePage;
class SoundPage;
class UsersPage;

class OverviewPage : public SettingsPage {
 public:
  OverviewPage(AppearancePage& appearance, SoundPage& sound, UsersPage& users, PulseClient& pulse,
               const std::function<void(const std::string&)>& open_page,
               const std::function<void(const std::string&)>& mark_page);
  Glib::ustring title() const override { return "Overview"; }
  Glib::ustring summary() const override { return ""; }
  void on_page_shown() override;

 private:
  void refresh_host();
  void refresh_quick();
  void open(const std::string& id);

  AppearancePage& appearance_;
  SoundPage& sound_;
  UsersPage& users_;
  PulseClient& pulse_;
  std::function<void(const std::string&)> open_page_;
  std::function<void(const std::string&)> mark_page_;
  int freeze_ = 0;
  bool volume_dragging_ = false;

  Gtk::Label* kicker_ = nullptr;
  Gtk::Label* version_ = nullptr;
  Gtk::Label* hostname_ = nullptr;
  Gtk::Label* processor_ = nullptr;
  Gtk::Label* memory_ = nullptr;
  Gtk::Label* graphics_ = nullptr;
  Gtk::Label* storage_ = nullptr;
  Gtk::Box* dark_row_ = nullptr;
  Gtk::Switch dark_;
  Gtk::Switch autologin_;
  Gtk::Scale volume_;
  sigc::connection pulse_connection_;
};

}  // namespace lcos

#endif
