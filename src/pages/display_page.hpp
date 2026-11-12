// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_DISPLAY_PAGE_HPP
#define LCOS_DISPLAY_PAGE_HPP

#include "backends/xfconf_store.hpp"
#include "backends/xrandr_backend.hpp"
#include "logic/display_layout.hpp"
#include "pages/settings_page.hpp"

#include <gdk/gdk.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/label.h>
#include <gtkmm/popover.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/switch.h>

#include <vector>

namespace lcos {

class DisplayPage : public SettingsPage {
 public:
  DisplayPage(XfconfStore& store, XrandrBackend& randr, Gtk::Window& parent);
  ~DisplayPage() override;

  Glib::ustring title() const override { return "Display"; }
  Glib::ustring summary() const override {
    return "Resolution, arrangement, and scaling. Changes ask before they stay.";
  }
  bool has_pending() const override { return dirty_; }
  std::string pending_detail() const override;
  bool discard_pending() override;
  bool commit_pending() override;
  std::string restore_session_hardware();
  bool uncommitted_hardware() const { return hardware_changed_; }
  void open_keep_preview();
  void close_keep_preview();
  void on_page_shown() override;

 private:
  // Not named on_draw: that overrides Gtk::Widget::on_draw, and returning
  // true there stops every child on this page from being painted.
  void paint_preview(const Cairo::RefPtr<Cairo::Context>& context);
  bool on_button_press(GdkEventButton* event);
  bool on_motion(GdkEventMotion* event);
  bool on_button_release(GdkEventButton* event);

  void load_hardware();
  static GdkFilterReturn screen_filter(GdkXEvent* xevent, GdkEvent* event, gpointer data);
  void rebuild_output_combo();
  void show_selected();
  void push_selected_from_fields();
  void mark_dirty();
  void redraw();
  int hit_test(double x, double y) const;
  void write_scheme(const std::string& scheme, bool track);
  // trigger_apply asks xfsettingsd to load Default. Revert must not: a fresh
  // profile has no Default scheme, and that request leaves the rejected mode.
  void clear_temp_scheme(bool trigger_apply);
  bool apply_saved_layout(const std::vector<OutputConfig>& layout, std::string& error);
  void reload_profiles();
  void load_profile(const std::string& id);
  void save_profile();
  void delete_profile();
  void identify();
  void ensure_primary();
  void paint_now();
  void sync_scale_buttons();
  void sync_arrange_buttons();
  void refresh_output_heading();
  bool on_arrange_draw(const Cairo::RefPtr<Cairo::Context>& context);
  OutputConfig* selected();
  const OutputConfig* selected() const;

  XfconfStore& store_;
  XrandrBackend& randr_;
  Gtk::Window& parent_;
  std::vector<OutputConfig> outputs_;
  std::vector<OutputConfig> session_open_;
  std::vector<OutputConfig> confirmed_;
  std::vector<OutputConfig> pending_revert_;
  bool dirty_ = false;
  bool applying_ = false;
  bool confirming_ = false;
  bool alive_ = true;
  sigc::connection paint_idle_;
  bool screen_watch_ = false;
  bool hardware_changed_ = false;
  GdkWindow* root_window_ = nullptr;
  int selected_index_ = 0;
  int freeze_ = 0;
  bool dragging_ = false;
  int grab_dx_ = 0;
  int grab_dy_ = 0;
  double origin_x_ = 0;
  double origin_y_ = 0;
  double view_scale_ = 1;

  Gtk::DrawingArea arrange_;
  Gtk::ScrolledWindow* scroll_ = nullptr;
  Gtk::Button* scale_buttons_[5] = {};
  Gtk::Button* custom_scale_ = nullptr;
  Gtk::Button* extend_button_ = nullptr;
  Gtk::Button* mirror_button_ = nullptr;
  Gtk::Popover* scale_popover_ = nullptr;
  Gtk::Label* output_heading_ = nullptr;
  Gtk::Window* keep_preview_ = nullptr;
  int preview_width_ = 720;
  int preview_height_ = 170;
  Gtk::ComboBoxText output_combo_;
  Gtk::Switch enabled_;
  Gtk::CheckButton primary_;
  Gtk::ComboBoxText resolution_;
  Gtk::ComboBoxText refresh_;
  Gtk::ComboBoxText rotation_;
  Gtk::ComboBoxText reflection_;
  Gtk::SpinButton scale_;
  Gtk::SpinButton x_pos_;
  Gtk::SpinButton y_pos_;
  Gtk::ComboBoxText on_connect_;
  Gtk::ComboBoxText auto_profile_;
  Gtk::ComboBoxText profiles_;
  Gtk::Label* status_ = nullptr;
};

}  // namespace lcos

#endif
