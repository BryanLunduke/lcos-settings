// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_ACCESSIBILITY_PAGE_HPP
#define LCOS_ACCESSIBILITY_PAGE_HPP

#include "backends/xfconf_store.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/checkbutton.h>
#include <gtkmm/notebook.h>
#include <gtkmm/spinbutton.h>

namespace lcos {

// xfce4-accessibility-settings 4.20. Assistive technologies live on the
// xfce4-session channel; keyboard and mouse filters live on accessibility.
class AccessibilityPage : public SettingsPage {
 public:
  explicit AccessibilityPage(XfconfStore& store);

  Glib::ustring title() const override { return "Accessibility"; }
  Glib::ustring summary() const override {
    return "Assistive technologies, sticky keys, slow keys, bounce keys, and mouse keys.";
  }
  void on_page_shown() override;
  int screenshot_count() const override { return 3; }
  void prepare_screenshot(int index) override;
  std::string screenshot_suffix(int index) const override;

 private:
  void load();

  XfconfStore& store_;
  int freeze_ = 0;
  Gtk::Notebook notebook_;

  Gtk::CheckButton assistive_;
  Gtk::CheckButton sticky_;
  Gtk::CheckButton sticky_lock_;
  Gtk::CheckButton sticky_two_;
  Gtk::CheckButton slow_;
  Gtk::SpinButton slow_delay_;
  Gtk::CheckButton bounce_;
  Gtk::SpinButton bounce_delay_;
  Gtk::CheckButton mouse_keys_;
  Gtk::SpinButton mouse_delay_;
  Gtk::SpinButton mouse_interval_;
  Gtk::SpinButton mouse_time_;
  Gtk::SpinButton mouse_speed_;
  Gtk::SpinButton mouse_curve_;
  Gtk::CheckButton find_cursor_;
};

}  // namespace lcos

#endif
