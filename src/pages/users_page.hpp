// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_USERS_PAGE_HPP
#define LCOS_USERS_PAGE_HPP

#include "backends/priv_client.hpp"
#include "logic/system_model.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/comboboxtext.h>
#include <gtkmm/image.h>
#include <gtkmm/liststore.h>
#include <gtkmm/treeview.h>

#include <string>
#include <vector>

namespace lcos {

class UsersPage : public SettingsPage {
 public:
  UsersPage(PrivSession& session, Gtk::Window& parent);

  Glib::ustring title() const override { return "Users"; }
  Glib::ustring summary() const override {
    return "Local accounts, passwords, pictures, and automatic login. Cancel restores this visit.";
  }
  void on_page_shown() override;

 private:
  class Columns : public Gtk::TreeModel::ColumnRecord {
   public:
    Columns() {
      add(name);
      add(real);
      add(kind);
      add(automatic);
    }
    Gtk::TreeModelColumn<Glib::ustring> name;
    Gtk::TreeModelColumn<Glib::ustring> real;
    Gtk::TreeModelColumn<Glib::ustring> kind;
    Gtk::TreeModelColumn<Glib::ustring> automatic;
  };

  void reload();
  void show_detail();
  void tell(const std::string& message);
  std::string selected_name();
  const Account* selected();
  void add_user();
  void remove_user();
  void change_password();
  void change_name();
  void change_picture();
  void change_type();
  void change_autologin();

  PrivSession& session_;
  Gtk::Window& parent_;
  int freeze_ = 0;
  std::string current_user_;
  std::string autologin_;
  std::vector<Account> accounts_;
  Columns columns_;
  Glib::RefPtr<Gtk::ListStore> store_;
  Gtk::TreeView view_;
  Gtk::ComboBoxText type_;
  Gtk::ComboBoxText autologin_combo_;
  Gtk::Image picture_;
};

}  // namespace lcos

#endif
