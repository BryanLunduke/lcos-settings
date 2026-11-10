// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_MEDIA_PAGE_HPP
#define LCOS_MEDIA_PAGE_HPP

#include "backends/xfconf_store.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/notebook.h>

namespace lcos {

// thunar-volman-settings 4.20. Commands are stored only after the entry changes.
class MediaPage : public SettingsPage {
 public:
  explicit MediaPage(XfconfStore& store);

  Glib::ustring title() const override { return "Removable Drives and Media"; }
  Glib::ustring summary() const override {
    return "Automount, autorun, cameras, multimedia, printers, and input devices.";
  }
  int screenshot_count() const override { return 5; }
  void prepare_screenshot(int index) override;
  std::string screenshot_suffix(int index) const override;

 private:
  XfconfStore& store_;
  int freeze_ = 0;
  Gtk::Notebook notebook_;
};

}  // namespace lcos

#endif
