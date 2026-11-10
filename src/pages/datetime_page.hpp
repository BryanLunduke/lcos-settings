// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_DATETIME_PAGE_HPP
#define LCOS_DATETIME_PAGE_HPP

#include "backends/priv_client.hpp"
#include "backends/xfconf_store.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/calendar.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/treeview.h>

#include <string>
#include <vector>

namespace lcos {

class DateTimePage : public SettingsPage {
 public:
  DateTimePage(XfconfStore& store, PrivSession& session, Gtk::Window& parent);

  Glib::ustring title() const override { return "Date & Time"; }
  Glib::ustring summary() const override {
    return "Time zone, clock, and automatic time. Administrator changes are undone by Cancel.";
  }
  void on_page_shown() override;

 private:
  class ZoneColumns : public Gtk::TreeModel::ColumnRecord {
   public:
    ZoneColumns() { add(name); }
    Gtk::TreeModelColumn<Glib::ustring> name;
  };

  void load_zones();
  void show_zones();
  void load_clock();
  void apply_zone(const std::string& zone);
  void tell(const std::string& message);
  bool chrony_enabled() const;

  XfconfStore& store_;
  PrivSession& session_;
  Gtk::Window& parent_;
  int freeze_ = 0;
  std::vector<std::string> zones_;
  std::string clock_property_;

  Gtk::Label* now_ = nullptr;
  Gtk::Label* clock_note_ = nullptr;
  Gtk::Entry search_;
  ZoneColumns zone_columns_;
  Glib::RefPtr<Gtk::ListStore> zone_store_;
  Gtk::TreeView zones_view_;
  Gtk::CheckButton ntp_;
  Gtk::Calendar calendar_;
  Gtk::SpinButton hour_;
  Gtk::SpinButton minute_;
  Gtk::SpinButton second_;
  Gtk::RadioButton hour24_;
  Gtk::RadioButton hour12_;
  sigc::connection clock_timer_;
};

}  // namespace lcos

#endif
