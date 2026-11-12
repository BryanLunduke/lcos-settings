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
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/revealer.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/stack.h>

#include <set>
#include <string>
#include <vector>

namespace lcos {

class MainWindow : public Gtk::ApplicationWindow {
 public:
  explicit MainWindow(XfconfStore& store);
  void dump_pages(const std::string& directory);
  void show_page(const std::string& id);
  void mark_page(const std::string& id);

 private:
  enum class PrivKind { Off, Accounts, Locale, System };

  struct NavItem {
    std::string id;
    std::string title;
    std::string icon;
    std::string keywords;
    std::string section;
    PrivKind priv = PrivKind::Off;
    SettingsPage* page = nullptr;
  };

  void build_sidebar();
  void select_nav(int index);
  void load_icons();
  void update_apply();
  void apply_filter();
  void set_compact(bool compact);
  void fit_to_screen();
  void remember_page(const std::string& id);
  std::string saved_page() const;
  void tell(const std::string& message);
  void keep_changes(bool close);
  void close_keep();
  void close_revert();
  void infobar_revert();
  void infobar_apply();
  bool page_marked(const NavItem& item) const;
  bool page_privileged(const NavItem& item) const;
  int nav_index(const std::string& id) const;
  bool on_delete_event(GdkEventAny* event) override;
  bool dump_step();

  XfconfStore& store_;
  PrivSession privileged_;
  XrandrBackend randr_;
  PointerBackend pointers_;
  PulseClient pulse_;

  struct Shot {
    int page = 0;
    int frame = 0;
    std::string name;
    int width = 0;
    int height = 0;
    bool unsaved = false;
    bool keep = false;
  };

  std::vector<NavItem> nav_;
  std::vector<Gtk::ListBoxRow*> rows_;
  std::vector<Gtk::Label*> row_labels_;
  std::vector<Gtk::Image*> nav_images_;
  std::vector<Gtk::Label*> row_dots_;
  std::vector<Gtk::ListBoxRow*> section_rows_;
  std::set<std::string> dirty_ids_;
  Gtk::SearchEntry search_;
  Gtk::ListBox sidebar_;
  Gtk::Stack stack_;
  Gtk::Image heading_icon_;
  Gtk::Label heading_;
  Gtk::Label summary_;
  Gtk::Revealer infobar_reveal_;
  Gtk::Label infobar_detail_;
  Gtk::Button infobar_revert_;
  Gtk::Button infobar_apply_;
  Gtk::Label status_;
  Gtk::Button ok_;
  Gtk::Button apply_;
  Gtk::Button cancel_;
  Gtk::ScrolledWindow side_scroll_;
  Gtk::Widget* side_chrome_ = nullptr;
  Gtk::Widget* page_head_ = nullptr;
  bool closing_ = false;
  bool compact_ = false;
  bool filtering_ = false;
  bool suppress_save_ = false;
  int current_index_ = -1;

  std::string dump_dir_;
  std::vector<Shot> shots_;
  int shot_index_ = 0;
  int dump_phase_ = 0;
};

}  // namespace lcos

#endif
