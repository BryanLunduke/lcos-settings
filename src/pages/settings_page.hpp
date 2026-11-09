// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_SETTINGS_PAGE_HPP
#define LCOS_SETTINGS_PAGE_HPP

#include <gtkmm/box.h>
#include <sigc++/signal.h>

namespace lcos {

// One category in the right-hand stack. Most pages write xfconf as the
// controls change. Display is the exception: geometry stays local until Apply.
class SettingsPage : public Gtk::Box {
 public:
  SettingsPage() : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0) {}
  ~SettingsPage() override = default;

  virtual Glib::ustring title() const = 0;
  virtual Glib::ustring summary() const = 0;
  virtual bool has_pending() const { return false; }
  // False means a confirm dialog was reverted and the window should stay open.
  virtual bool commit_pending() { return true; }
  virtual void on_page_shown() {}

  sigc::signal<void> pending_changed;
};

}  // namespace lcos

#endif
