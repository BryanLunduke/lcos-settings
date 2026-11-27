// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_NETWORK_PAGE_HPP
#define LCOS_NETWORK_PAGE_HPP

#include "backends/nm_client.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/spinbutton.h>

#include <map>
#include <string>

namespace lcos {

class NetworkPage : public SettingsPage {
 public:
  NetworkPage();
  Glib::ustring title() const override { return "Network"; }
  Glib::ustring summary() const override {
    return "Wired, Wi-Fi, saved connections, and VPN import.";
  }
  bool has_pending() const override { return form_dirty_ || !baseline_.empty(); }
  bool commit_pending() override;
  void revert_page() override;
  bool discard_pending() override;
  std::string pending_detail() const override;
  void on_page_shown() override;

 private:
  struct Baseline {
    NetConnection connection;
    bool password = false;
    std::string secret;
  };

  void refresh(bool keep_form);
  void fill_form(const NetConnection* connection);
  void mark_form();
  bool save_form();
  NetConnection edited_connection() const;
  const NetConnection* selected() const;
  void tell(const std::string& message);

  NmClient client_;
  NetSnapshot snapshot_;
  std::string selected_;
  std::string form_uuid_;
  bool form_dirty_ = false;
  int freeze_ = 0;
  std::map<std::string, Baseline> baseline_;

  Gtk::Label* status_ = nullptr;
  Gtk::Label* wired_ = nullptr;
  Gtk::Label* wifi_ = nullptr;
  Gtk::ListBox list_;
  Gtk::Entry name_;
  Gtk::CheckButton autoconnect_;
  Gtk::ComboBoxText method_;
  Gtk::Entry address_;
  Gtk::SpinButton prefix_;
  Gtk::Entry gateway_;
  Gtk::Entry dns_;
  Gtk::Entry password_;
  Gtk::CheckButton show_password_;
  Gtk::Box* manual_ = nullptr;
  Gtk::Box* wifi_box_ = nullptr;
  Gtk::Box* editor_ = nullptr;
  Gtk::Label* plugin_note_ = nullptr;
};

}  // namespace lcos

#endif
