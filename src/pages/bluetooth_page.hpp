// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_BLUETOOTH_PAGE_HPP
#define LCOS_BLUETOOTH_PAGE_HPP

#include "backends/bluez_client.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/switch.h>

#include <map>
#include <string>

namespace lcos {

class BluetoothPage : public SettingsPage {
 public:
  BluetoothPage();
  ~BluetoothPage() override;
  Glib::ustring title() const override { return "Bluetooth"; }
  Glib::ustring summary() const override { return "Adapter, pairing, and trusted devices."; }
  bool has_pending() const override { return dirty_; }
  bool commit_pending() override;
  void revert_page() override;
  bool discard_pending() override;
  std::string pending_detail() const override;
  void on_page_shown() override;

 private:
  struct AdapterBaseline {
    bool powered = false;
    bool discoverable = false;
    std::string name;
  };

  void refresh();
  void rebuild();
  void tell(const std::string& message);
  void remember_adapter();
  const BtDevice* selected() const;
  bool scan_tick();

  BluezClient client_;
  BtSnapshot snapshot_;
  std::string selected_;
  bool dirty_ = false;
  bool have_adapter_ = false;
  AdapterBaseline original_;
  std::map<std::string, bool> original_trust_;
  int freeze_ = 0;
  int scans_left_ = 0;
  sigc::connection scan_timer_;

  Gtk::Label* status_ = nullptr;
  Gtk::Switch powered_;
  Gtk::Switch discoverable_;
  Gtk::Entry name_;
  Gtk::ListBox devices_;
  Gtk::CheckButton trusted_;
  Gtk::Button scan_;
  Gtk::Box* body_ = nullptr;
};

}  // namespace lcos

#endif
