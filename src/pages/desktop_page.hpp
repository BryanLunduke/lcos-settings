// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_DESKTOP_PAGE_HPP
#define LCOS_DESKTOP_PAGE_HPP

#include "backends/xfconf_store.hpp"
#include "backends/xrandr_backend.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/colorbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/filechooserbutton.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/label.h>
#include <gtkmm/notebook.h>
#include <gtkmm/spinbutton.h>

#include <string>
#include <vector>

namespace lcos {

class DesktopPage : public SettingsPage {
 public:
  DesktopPage(XfconfStore& store, XrandrBackend& randr);

  Glib::ustring title() const override { return "Desktop"; }
  Glib::ustring summary() const override {
    return "Background, desktop icons, and desktop menus from xfdesktop.";
  }
  void on_page_shown() override;
  int screenshot_count() const override { return 3; }
  void prepare_screenshot(int index) override;
  std::string screenshot_suffix(int index) const override;

 private:
  void reload_monitors();
  void reload_fields();
  void scan_folder();
  void choose_image(const std::string& path);
  std::string property(const std::string& leaf) const;
  int workspace_index() const;
  void write_uint(const char* leaf, unsigned value);
  void write_bool(const char* leaf, bool value);
  void build_icons();
  void build_menus();
  void load_icons();
  void load_menus();
  void bind_desktop_bool(Gtk::CheckButton& check, const char* prop, bool fallback);
  void set_icon_sensitivity();

  XfconfStore& store_;
  XrandrBackend& randr_;
  int freeze_ = 0;
  Gtk::Notebook notebook_;
  std::vector<std::string> image_paths_;

  Gtk::ComboBoxText monitor_;
  Gtk::SpinButton workspace_;
  Gtk::CheckButton all_workspaces_;
  Gtk::FileChooserButton folder_;
  Gtk::FlowBox images_;
  Gtk::Label* image_caption_ = nullptr;
  Gtk::ComboBoxText style_;
  Gtk::ComboBoxText color_style_;
  Gtk::ColorButton color1_;
  Gtk::ColorButton color2_;
  Gtk::CheckButton cycle_;
  Gtk::ComboBoxText cycle_period_;
  Gtk::SpinButton cycle_timer_;
  Gtk::CheckButton cycle_random_;

  Gtk::ComboBoxText icon_style_;
  Gtk::SpinButton icon_size_;
  Gtk::CheckButton custom_font_;
  Gtk::SpinButton font_size_;
  Gtk::CheckButton custom_font_color_;
  Gtk::ColorButton font_color_;
  Gtk::CheckButton custom_label_bg_;
  Gtk::ColorButton label_bg_;
  Gtk::CheckButton show_tooltips_;
  Gtk::SpinButton tooltip_size_;
  Gtk::ComboBoxText gravity_;
  Gtk::CheckButton primary_display_;
  Gtk::CheckButton single_click_;
  Gtk::CheckButton single_underline_;
  Gtk::CheckButton confirm_sort_;
  Gtk::CheckButton hidden_files_;
  Gtk::CheckButton thumbnails_;
  Gtk::CheckButton delete_option_;
  Gtk::CheckButton folders_first_;
  Gtk::CheckButton show_home_;
  Gtk::CheckButton show_filesystem_;
  Gtk::CheckButton show_trash_;
  Gtk::CheckButton show_removable_;
  Gtk::CheckButton show_fixed_;
  Gtk::CheckButton show_device_removable_;
  Gtk::CheckButton show_network_;
  Gtk::CheckButton show_unknown_;
  Gtk::Box* file_icons_ = nullptr;

  Gtk::CheckButton menu_show_;
  Gtk::CheckButton menu_icons_;
  Gtk::CheckButton winlist_show_;
  Gtk::CheckButton winlist_icons_;
  Gtk::CheckButton winlist_all_;
  Gtk::CheckButton winlist_sticky_;
  Gtk::CheckButton winlist_urgent_;
  Gtk::CheckButton winlist_names_;
  Gtk::CheckButton winlist_submenus_;
  Gtk::CheckButton winlist_add_;
  Gtk::Box* menu_options_ = nullptr;
  Gtk::Box* winlist_options_ = nullptr;
  Gtk::Box* winlist_name_options_ = nullptr;
};

}  // namespace lcos

#endif
