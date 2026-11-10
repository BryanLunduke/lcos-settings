// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_NOTIFICATIONS_PAGE_HPP
#define LCOS_NOTIFICATIONS_PAGE_HPP

#include "backends/xfconf_store.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/entry.h>
#include <gtkmm/liststore.h>
#include <gtkmm/notebook.h>
#include <gtkmm/scale.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/treeview.h>
#include <gtkmm/window.h>

namespace lcos {

// xfce4-notifyd-config 0.9. The log viewer itself is the notifyd log service;
// this page stores the same xfconf settings and can send a preview.
class NotificationsPage : public SettingsPage {
 public:
  NotificationsPage(XfconfStore& store, Gtk::Window& parent);

  Glib::ustring title() const override { return "Notifications"; }
  Glib::ustring summary() const override {
    return "Theme, position, timeout, and per-application notification settings.";
  }
  void on_page_shown() override;
  int screenshot_count() const override { return 4; }
  void prepare_screenshot(int index) override;
  std::string screenshot_suffix(int index) const override;

 private:
  class AppColumns : public Gtk::TreeModel::ColumnRecord {
   public:
    AppColumns() {
      add(name);
      add(muted);
      add(critical);
      add(logged);
    }
    Gtk::TreeModelColumn<Glib::ustring> name;
    Gtk::TreeModelColumn<bool> muted;
    Gtk::TreeModelColumn<bool> critical;
    Gtk::TreeModelColumn<bool> logged;
  };

  void load();
  void load_apps();
  void load_themes();
  void set_membership(const char* prop, const std::string& app, bool member);
  void preview();
  void remove_selected();

  XfconfStore& store_;
  Gtk::Window& parent_;
  int freeze_ = 0;
  Gtk::Notebook notebook_;

  Gtk::ComboBoxText display_fields_;
  Gtk::CheckButton do_not_disturb_;
  Gtk::CheckButton gauge_ignores_;
  Gtk::ComboBoxText show_on_;
  Gtk::CheckButton mute_sounds_;

  Gtk::ComboBoxText theme_;
  Gtk::ComboBoxText location_;
  Gtk::Scale opacity_;
  Gtk::CheckButton min_width_enabled_;
  Gtk::SpinButton min_width_;
  Gtk::CheckButton expire_enabled_;
  Gtk::SpinButton expire_timeout_;
  Gtk::CheckButton expire_override_;
  Gtk::CheckButton fadeout_;
  Gtk::CheckButton slideout_;
  Gtk::CheckButton text_with_gauge_;
  Gtk::ComboBoxText datetime_;
  Gtk::Entry datetime_custom_;

  AppColumns app_columns_;
  Glib::RefPtr<Gtk::ListStore> apps_;
  Gtk::TreeView apps_view_;

  Gtk::CheckButton log_enabled_;
  Gtk::ComboBoxText log_level_;
  Gtk::ComboBoxText log_apps_;
  Gtk::CheckButton log_limit_;
  Gtk::SpinButton log_size_;
};

}  // namespace lcos

#endif
