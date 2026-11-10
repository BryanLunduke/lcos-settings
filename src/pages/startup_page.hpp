// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_STARTUP_PAGE_HPP
#define LCOS_STARTUP_PAGE_HPP

#include "backends/priv_client.hpp"
#include "backends/xfconf_store.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/checkbutton.h>
#include <gtkmm/liststore.h>
#include <gtkmm/notebook.h>
#include <gtkmm/treeview.h>

#include <map>
#include <string>

namespace lcos {

class StartupPage : public SettingsPage {
 public:
  StartupPage(XfconfStore& store, PrivSession& session, Gtk::Window& parent);

  Glib::ustring title() const override { return "Startup"; }
  Glib::ustring summary() const override {
    return "Applications started at login, and the session options from xfce4-session.";
  }
  bool has_pending() const override;
  bool commit_pending() override;
  void revert_page() override;
  int screenshot_count() const override { return 2; }
  void prepare_screenshot(int index) override;
  std::string screenshot_suffix(int index) const override;

 private:
  class AppColumns : public Gtk::TreeModel::ColumnRecord {
   public:
    AppColumns() {
      add(enabled);
      add(name);
      add(command);
      add(id);
      add(removable);
      add(show_in_xfce);
    }
    Gtk::TreeModelColumn<bool> enabled;
    Gtk::TreeModelColumn<Glib::ustring> name;
    Gtk::TreeModelColumn<Glib::ustring> command;
    Gtk::TreeModelColumn<Glib::ustring> id;
    Gtk::TreeModelColumn<bool> removable;
    Gtk::TreeModelColumn<bool> show_in_xfce;
  };

  void reload_apps();
  void reload_sessions();
  void remember_autostart();
  void remember_sessions();
  void restore_files();
  std::string user_autostart_dir() const;
  std::string selected_id();
  void toggle_selected(const std::string& path);
  void add_app();
  void edit_app();
  void remove_app();
  void clear_sessions();

  XfconfStore& store_;
  PrivSession& session_;
  Gtk::Window& parent_;
  int freeze_ = 0;
  bool files_dirty_ = false;
  std::map<std::string, std::string> autostart_snapshot_;
  std::map<std::string, std::string> session_snapshot_;
  std::string session_dir_;

  Gtk::Notebook notebook_;
  AppColumns columns_;
  Glib::RefPtr<Gtk::ListStore> apps_;
  Gtk::TreeView apps_view_;
  Gtk::TreeView sessions_view_;
  Glib::RefPtr<Gtk::ListStore> sessions_;
  Gtk::CheckButton prompt_;
  Gtk::CheckButton save_;
  Gtk::CheckButton chooser_;
  Gtk::CheckButton gnome_;
  Gtk::CheckButton kde_;
  Gtk::CheckButton tcp_;
};

}  // namespace lcos

#endif
