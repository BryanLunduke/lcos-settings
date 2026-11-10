// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_MAIN_WINDOW_HPP
#define LCOS_MAIN_WINDOW_HPP

#include "backends/pointer_backend.hpp"
#include "backends/priv_client.hpp"
#include "backends/pulse_client.hpp"
#include "backends/xfconf_store.hpp"
#include "backends/xrandr_backend.hpp"
#include "pages/settings_page.hpp"

#include <gtkmm/applicationwindow.h>
#include <gtkmm/button.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/image.h>
#include <gtkmm/listbox.h>
#include <gtkmm/stack.h>

#include <string>
#include <vector>

namespace lcos {

class MainWindow : public Gtk::ApplicationWindow {
 public:
  explicit MainWindow(XfconfStore& store);
  void dump_pages(const std::string& directory);

 private:
  void select_page(int index);
  void load_nav_icons();
  void update_apply();
  void tell(const std::string& message);
  void close_keep();
  void close_revert();
  bool on_delete_event(GdkEventAny* event) override;
  bool dump_step();

  XfconfStore& store_;
  PrivSession privileged_;
  XrandrBackend randr_;
  PointerBackend pointers_;
  PulseClient pulse_;
  struct NavItem {
    std::string id;
    std::string title;
    std::string icon;
  };
  struct Shot {
    int page = 0;
    int frame = 0;
    std::string name;
    int width = 0;
    int height = 0;
  };

  std::vector<SettingsPage*> pages_;
  std::vector<NavItem> nav_;
  std::vector<Gtk::Image*> nav_images_;
  Gtk::ListBox sidebar_;
  Gtk::Stack stack_;
  Gtk::Label* heading_ = nullptr;
  Gtk::Label* summary_ = nullptr;
  Gtk::Label* status_ = nullptr;
  Gtk::Button* apply_ = nullptr;
  bool closing_ = false;

  std::string dump_dir_;
  std::vector<Shot> shots_;
  int shot_index_ = 0;
  int dump_phase_ = 0;
};

}  // namespace lcos

#endif
