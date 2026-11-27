// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_PANEL_PAGE_HPP
#define LCOS_PANEL_PAGE_HPP

#include "backends/xfconf_store.hpp"
#include "logic/panel_model.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/colorbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scale.h>
#include <gtkmm/spinbutton.h>

#include <string>
#include <vector>

namespace lcos {

class PanelPage : public SettingsPage {
 public:
  explicit PanelPage(XfconfStore& store);
  Glib::ustring title() const override { return "Panel"; }
  Glib::ustring summary() const override { return "Position, size, and the items on the panel."; }
  void on_page_shown() override { load(); }
  void reload_from_store() override { load(); }

 private:
  struct Item {
    int id = 0;
    std::string name;
  };

  void load();
  void show_panel(int id);
  void write_position();
  void write_plugins(const std::vector<int>& ids);
  std::vector<int> plugin_ids() const;
  std::vector<int> known_plugin_ids() const;
  std::string prop(const std::string& leaf) const;
  void tell(const std::string& message);

  XfconfStore& store_;
  int panel_id_ = -1;
  std::vector<int> panels_;
  std::vector<Item> items_;
  bool sample_ = false;
  int freeze_ = 0;

  Gtk::Label* status_ = nullptr;
  Gtk::Label* panel_caption_ = nullptr;
  Gtk::Widget* chooser_ = nullptr;
  Gtk::ComboBoxText panel_;
  Gtk::ComboBoxText position_;
  Gtk::SpinButton size_;
  Gtk::SpinButton length_;
  Gtk::ComboBoxText autohide_;
  Gtk::CheckButton locked_;
  Gtk::ComboBoxText background_;
  Gtk::ColorButton color_;
  Gtk::Scale opacity_;
  Gtk::ListBox items_list_;
  Gtk::ComboBoxText add_kind_;
  Gtk::Box* body_ = nullptr;
};

}  // namespace lcos

#endif
