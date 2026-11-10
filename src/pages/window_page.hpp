// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_WINDOW_PAGE_HPP
#define LCOS_WINDOW_PAGE_HPP

#include "backends/xfconf_store.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/liststore.h>
#include <gtkmm/notebook.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/scale.h>
#include <gtkmm/treeview.h>
#include <gtkmm/window.h>

#include <map>

namespace lcos {

// Focus, Keyboard, and Advanced from xfwm4-settings 4.20. Theme, font, and
// title alignment stay on Appearance. Workspace count and the wrap-workspaces
// frame are omitted; LCOS stays on a single workspace.
class WindowPage : public SettingsPage {
 public:
  WindowPage(XfconfStore& store, Gtk::Window& parent);

  Glib::ustring title() const override { return "Window Manager"; }
  Glib::ustring summary() const override {
    return "Focus, window-manager shortcuts, and advanced behavior. Changes apply immediately.";
  }
  void on_page_shown() override;
  int screenshot_count() const override { return 3; }
  void prepare_screenshot(int index) override;
  std::string screenshot_suffix(int index) const override;

 private:
  class ShortcutColumns : public Gtk::TreeModel::ColumnRecord {
   public:
    ShortcutColumns() {
      add(feature);
      add(label);
      add(accel);
    }
    Gtk::TreeModelColumn<Glib::ustring> feature;
    Gtk::TreeModelColumn<Glib::ustring> label;
    Gtk::TreeModelColumn<Glib::ustring> accel;
  };

  void load();
  void load_shortcuts();
  void edit_shortcut();
  void clear_shortcut();
  void reset_shortcuts();
  void apply_shortcut(const std::string& feature, const std::string& accel);
  std::map<std::string, std::string> shortcut_values() const;

  XfconfStore& store_;
  Gtk::Window& parent_;
  int freeze_ = 0;
  Gtk::Notebook notebook_;

  Gtk::RadioButton click_focus_;
  Gtk::RadioButton follow_focus_;
  Gtk::Scale focus_delay_;
  Gtk::CheckButton focus_new_;
  Gtk::CheckButton raise_on_focus_;
  Gtk::Scale raise_delay_;
  Gtk::CheckButton raise_on_click_;

  Gtk::CheckButton snap_border_;
  Gtk::CheckButton snap_windows_;
  Gtk::Scale snap_width_;
  Gtk::CheckButton box_move_;
  Gtk::CheckButton box_resize_;
  Gtk::ComboBoxText double_click_;

  ShortcutColumns shortcut_columns_;
  Glib::RefPtr<Gtk::ListStore> shortcuts_;
  Gtk::TreeView shortcuts_view_;
};

}  // namespace lcos

#endif
