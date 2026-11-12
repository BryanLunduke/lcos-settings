// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_USERS_PAGE_HPP
#define LCOS_USERS_PAGE_HPP

#include "backends/priv_client.hpp"
#include "logic/system_model.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/button.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/entry.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/switch.h>

#include <map>
#include <string>
#include <vector>

namespace lcos {

class UsersPage : public SettingsPage {
 public:
  UsersPage(PrivSession& session, Gtk::Window& parent);

  Glib::ustring title() const override { return "Users"; }
  Glib::ustring summary() const override {
    return "Accounts, passwords, pictures, and automatic login.";
  }
  bool has_pending() const override { return !staged_names_.empty(); }
  int unsaved_count() const override { return static_cast<int>(staged_names_.size()); }
  std::string pending_detail() const override;
  bool commit_pending() override;
  bool discard_pending() override;
  void revert_page() override;
  void on_page_shown() override;

  bool automatic_for_current() const;
  void set_automatic_for_current(bool on);
  // Local edit only, used while capturing the unsaved-changes screenshot.
  void stage_screenshot_edit();

 private:
  void reload();
  void show_detail();
  void tell(const std::string& message);
  std::string selected_name() const;
  const Account* selected() const;
  const Account* find_account(const std::string& name) const;
  void add_user();
  void remove_user();
  void change_password();
  void change_picture();
  void change_type();
  void change_autologin();
  void on_name_changed();
  void refresh_row_dots();

  PrivSession& session_;
  Gtk::Window& parent_;
  int freeze_ = 0;
  std::string current_user_;
  std::string autologin_;
  std::vector<Account> accounts_;
  std::map<std::string, std::string> staged_names_;
  std::vector<std::pair<Gtk::ListBoxRow*, std::string>> rows_;

  Gtk::ListBox list_;
  Gtk::ComboBoxText type_;
  Gtk::Switch autologin_switch_;
  Gtk::Entry name_entry_;
  Gtk::Image picture_;
  Gtk::Label* heading_name_ = nullptr;
  Gtk::Label* heading_meta_ = nullptr;
  Gtk::Label* username_value_ = nullptr;
  Gtk::Label* autologin_hint_ = nullptr;
  Gtk::Label* name_dot_ = nullptr;
  Gtk::Button* remove_button_ = nullptr;
  Gtk::Button* picture_button_ = nullptr;
};

}  // namespace lcos

#endif
