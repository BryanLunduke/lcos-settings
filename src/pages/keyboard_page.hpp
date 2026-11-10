// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_KEYBOARD_PAGE_HPP
#define LCOS_KEYBOARD_PAGE_HPP

#include "backends/xfconf_store.hpp"
#include "logic/xkb_rules.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/liststore.h>
#include <gtkmm/scale.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/treeview.h>

namespace lcos {

class KeyboardPage : public SettingsPage {
 public:
  explicit KeyboardPage(XfconfStore& store, Gtk::Window& parent);
  Glib::ustring title() const override { return "Keyboard"; }
  Glib::ustring summary() const override {
    return "Repeat, layouts, and application shortcuts. Changes apply immediately.";
  }

 private:
  class LayoutColumns : public Gtk::TreeModel::ColumnRecord {
   public:
    LayoutColumns() {
      add(layout);
      add(variant);
      add(layout_id);
      add(variant_id);
    }
    Gtk::TreeModelColumn<Glib::ustring> layout;
    Gtk::TreeModelColumn<Glib::ustring> variant;
    Gtk::TreeModelColumn<Glib::ustring> layout_id;
    Gtk::TreeModelColumn<Glib::ustring> variant_id;
  };

  class ShortcutColumns : public Gtk::TreeModel::ColumnRecord {
   public:
    ShortcutColumns() {
      add(accel);
      add(command);
      add(startup);
      add(custom);
      add(origin);
    }
    Gtk::TreeModelColumn<Glib::ustring> accel;
    Gtk::TreeModelColumn<Glib::ustring> command;
    Gtk::TreeModelColumn<bool> startup;
    Gtk::TreeModelColumn<bool> custom;
    Gtk::TreeModelColumn<Glib::ustring> origin;
  };

  void load_rules();
  void load_behavior();
  void load_layouts();
  void write_layouts();
  void load_shortcuts();
  void add_shortcut();
  void edit_shortcut();
  void remove_shortcut();
  void set_layout_controls_sensitive();
  std::string layout_description(const std::string& id) const;
  std::string variant_description(const std::string& layout, const std::string& variant) const;
  void refill_variant_combo();

  XfconfStore& store_;
  Gtk::Window& parent_;
  XkbRules rules_;
  int freeze_ = 0;

  Gtk::CheckButton repeat_;
  Gtk::Scale delay_;
  Gtk::Scale rate_;
  Gtk::CheckButton blink_;
  Gtk::SpinButton blink_time_;
  Gtk::CheckButton numlock_;
  Gtk::CheckButton system_defaults_;
  Gtk::ComboBoxText model_;
  Gtk::TreeView layouts_view_;
  LayoutColumns layout_columns_;
  Glib::RefPtr<Gtk::ListStore> layouts_;
  Gtk::ComboBoxText variant_;
  Gtk::ComboBoxText group_;
  Gtk::ComboBoxText compose_;
  Gtk::TreeView shortcuts_view_;
  ShortcutColumns shortcut_columns_;
  Glib::RefPtr<Gtk::ListStore> shortcuts_;
};

}  // namespace lcos

#endif
