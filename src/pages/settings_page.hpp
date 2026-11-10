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
  // Files outside xfconf (helpers.rc, mimeapps.list) restore themselves here.
  virtual void revert_page() {}
  virtual void on_page_shown() {}
  // Extra frames for sections that contain their own tabs.
  virtual int screenshot_count() const { return 1; }
  virtual void prepare_screenshot(int index) { (void)index; }
  virtual std::string screenshot_suffix(int index) const {
    (void)index;
    return {};
  }

  sigc::signal<void> pending_changed;
};

}  // namespace lcos

#endif
