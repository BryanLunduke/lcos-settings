// SPDX-License-Identifier: GPL-3.0-or-later

#include "main_window.hpp"

#include "pages/accessibility_page.hpp"
#include "pages/appearance_page.hpp"
#include "pages/datetime_page.hpp"
#include "pages/desktop_page.hpp"
#include "pages/display_page.hpp"
#include "pages/keyboard_page.hpp"
#include "pages/language_page.hpp"
#include "pages/media_page.hpp"
#include "pages/mouse_page.hpp"
#include "pages/notifications_page.hpp"
#include "pages/overview_page.hpp"
#include "pages/power_page.hpp"
#include "pages/software_page.hpp"
#include "pages/sound_page.hpp"
#include "pages/startup_page.hpp"
#include "pages/users_page.hpp"
#include "pages/window_page.hpp"
#include "pages/window_tweaks_page.hpp"
#include "ui_util.hpp"

#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <gdkmm/display.h>
#include <gdkmm/general.h>
#include <gdkmm/pixbuf.h>
#include <glibmm/markup.h>
#include <glibmm/fileutils.h>
#include <glibmm/keyfile.h>
#include <glibmm/miscutils.h>
#include <glibmm/main.h>
#include <gtkmm/bin.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/image.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/separator.h>

#include <algorithm>
#include <cstdio>
#include <iostream>

namespace lcos {
namespace {

// Narrow enough to match the mockup, wide enough for a wrapped
// "Removable Drives and Media" plus the icon.
constexpr int kSidebarWidth = 220;
constexpr int kSidebarMinWidth = 64;
constexpr int kWindowMinWidth = 760;
constexpr int kWindowMinHeight = 480;

// Minimum width is the icon rail. Natural width is the labelled sidebar.
// A size request of 220 would stop the window from shrinking back to 760.
class SideChrome : public Gtk::Box {
 public:
  SideChrome() : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0) {}

  void set_wide(bool wide) {
    if (wide_ == wide) {
      return;
    }
    wide_ = wide;
    queue_resize();
  }

 protected:
  void get_preferred_width_vfunc(int& minimum_width, int& natural_width) const override {
    minimum_width = kSidebarMinWidth;
    natural_width = wide_ ? kSidebarWidth : kSidebarMinWidth;
  }

 private:
  bool wide_ = true;
};

std::string state_path() {
  return Glib::build_filename(Glib::get_user_config_dir(), "lcos-settings", "state.ini");
}

bool animations_enabled() {
  gboolean enabled = TRUE;
  if (auto* settings = gtk_settings_get_default()) {
    g_object_get(settings, "gtk-enable-animations", &enabled, nullptr);
  }
  return enabled != FALSE;
}

Glib::RefPtr<Gdk::Pixbuf> load_named_icon(const std::string& name, int size, int scale) {
  auto legacy = Gtk::IconTheme::create();
  legacy->set_custom_theme("AdwaitaLegacy");
  auto fallback = Gtk::IconTheme::get_default();
  const std::string aliases[] = {name, name == "input-mouse" ? "preferences-desktop-peripherals" : "",
                                 name == "preferences-other" ? "applications-other" : "",
                                 name == "battery" ? "battery-full-charged" : ""};
  for (const auto& candidate : aliases) {
    if (candidate.empty()) {
      continue;
    }
    const Glib::RefPtr<Gtk::IconTheme> themes[] = {legacy, fallback};
  for (const auto& theme : themes) {
      if (!theme) {
        continue;
      }
      try {
        auto pixbuf = theme->load_icon(candidate, size, scale, Gtk::ICON_LOOKUP_FORCE_SIZE);
        if (pixbuf) {
          return pixbuf;
        }
      } catch (const Glib::Error&) {
      }
    }
  }
  return {};
}

}  // namespace

MainWindow::MainWindow(XfconfStore& store)
    : store_(store),
      randr_(gdk_x11_display_get_xdisplay(gdk_display_get_default())),
      pointers_(gdk_x11_display_get_xdisplay(gdk_display_get_default())),
      infobar_reveal_(),
      infobar_revert_("_Revert", true),
      infobar_apply_("_Apply", true),
      ok_("_OK", true),
      apply_("_Apply", true),
      cancel_("_Cancel", true) {
  set_title("LCOS Command Center");
  set_resizable(true);
  get_style_context()->add_class("lcos-app");
  set_icon_name("org.lunduke.LcosSettings");
  if (g_file_test("data/icons/hicolor/48x48/apps/org.lunduke.LcosSettings.png", G_FILE_TEST_EXISTS)) {
    try {
      set_icon_from_file("data/icons/hicolor/48x48/apps/org.lunduke.LcosSettings.png");
    } catch (const Glib::Error&) {
    }
  }

  auto* display = Gtk::manage(new DisplayPage(store_, randr_, *this));
  auto* desktop = Gtk::manage(new DesktopPage(store_, randr_));
  auto* appearance = Gtk::manage(new AppearancePage(store_));
  auto* windows = Gtk::manage(new WindowPage(store_, *this));
  auto* tweaks = Gtk::manage(new WindowTweaksPage(store_));
  auto* keyboard = Gtk::manage(new KeyboardPage(store_, *this));
  auto* mouse = Gtk::manage(new MousePage(store_, pointers_));
  auto* accessibility = Gtk::manage(new AccessibilityPage(store_));
  auto* power = Gtk::manage(new PowerPage(store_, randr_));
  auto* notifications = Gtk::manage(new NotificationsPage(store_, *this));
  auto* media = Gtk::manage(new MediaPage(store_));
  auto* sound = Gtk::manage(new SoundPage(store_, pulse_));
  auto* datetime = Gtk::manage(new DateTimePage(store_, privileged_, *this));
  auto* language = Gtk::manage(new LanguagePage(privileged_, *this));
  auto* users = Gtk::manage(new UsersPage(privileged_, *this));
  auto* startup = Gtk::manage(new StartupPage(store_, privileged_, *this));
  auto* software = Gtk::manage(new SoftwarePage());
  auto* overview = Gtk::manage(new OverviewPage(
      *appearance, *sound, *users, pulse_, privileged_,
      [this](const std::string& id) { show_page(id); },
      [this](const std::string& id) { mark_page(id); }));

  nav_ = {
      {"overview", "Overview", "computer", "system version hostname memory processor graphics storage quick settings dark style volume", "", PrivKind::Off, overview},
      {"appearance", "Appearance", "preferences-desktop-theme", "style theme icons font dpi antialias hinting toolbar scaling", "Look and Feel", PrivKind::Off, appearance},
      {"desktop", "Desktop", "preferences-desktop-wallpaper", "wallpaper background icons menus", "Look and Feel", PrivKind::Off, desktop},
      {"window-manager", "Window Manager", "preferences-system-windows", "focus shortcuts title bar theme placement", "Look and Feel", PrivKind::Off, windows},
      {"window-tweaks", "Window Manager Tweaks", "preferences-system", "compositor cycling focus accessibility zoom placement tweaks", "Look and Feel", PrivKind::Off, tweaks},
      {"notifications", "Notifications", "preferences-system-notifications", "notify theme position log applications", "Look and Feel", PrivKind::Off, notifications},
      {"display", "Display", "preferences-desktop-display", "resolution refresh scale rotation mirror extend monitor primary reflection", "Hardware", PrivKind::Off, display},
      {"sound", "Sound", "audio-volume-high", "volume mute output input microphone profile", "Hardware", PrivKind::Off, sound},
      {"power", "Power", "battery", "battery sleep lid blank brightness lock", "Hardware", PrivKind::Off, power},
      {"keyboard", "Keyboard", "preferences-desktop-keyboard", "layout repeat shortcut model compose", "Hardware", PrivKind::Off, keyboard},
      {"mouse", "Mouse", "input-mouse", "pointer touchpad scrolling cursor speed", "Hardware", PrivKind::Off, mouse},
      {"removable-media", "Removable Drives and Media", "drive-removable-media", "usb camera mount cd dvd printer tablet", "Hardware", PrivKind::Off, media},
      {"date-time", "Date and Time", "appointment-soon", "clock timezone ntp calendar", "System", PrivKind::System, datetime},
      {"language", "Language", "preferences-desktop-locale", "locale region format language", "System", PrivKind::Locale, language},
      {"users", "Users", "system-users", "account password autologin real name picture administrator", "System", PrivKind::Accounts, users},
      {"startup", "Startup", "system-run", "autostart session login", "System", PrivKind::Off, startup},
      {"default-software", "Default Software", "preferences-other", "browser mail terminal mime utilities", "System", PrivKind::Off, software},
      {"accessibility", "Accessibility", "preferences-desktop-accessibility", "keyboard mouse assistive sticky keys", "System", PrivKind::Off, accessibility},
  };

  auto* root = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  add(*root);
  auto* body = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
  prepare_page_scroll(side_scroll_);
  side_scroll_.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  side_scroll_.set_size_request(-1, -1);
  side_scroll_.set_min_content_width(0);
  sidebar_.get_style_context()->add_class("lcos-sidebar");
  sidebar_.set_selection_mode(Gtk::SELECTION_BROWSE);
  auto* side = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  search_.get_style_context()->add_class("lcos-search");
  search_.set_placeholder_text("Search settings");
  auto* side_box = Gtk::manage(new SideChrome());
  side_box->get_style_context()->add_class("lcos-sidebar");
  side_box->pack_start(search_, Gtk::PACK_SHRINK);
  build_sidebar();
  side->pack_start(sidebar_, Gtk::PACK_EXPAND_WIDGET);
  if (search_empty_ != nullptr) {
    side->pack_start(*search_empty_, Gtk::PACK_SHRINK);
  }
  side_scroll_.add(*side);
  side_box->pack_start(side_scroll_, Gtk::PACK_EXPAND_WIDGET);
  side_box->set_halign(Gtk::ALIGN_START);
  side_box->set_valign(Gtk::ALIGN_FILL);
  side_box->set_hexpand(false);
  side_box->set_vexpand(true);
  side_chrome_ = side_box;
  body->pack_start(*side_box, Gtk::PACK_SHRINK);

  const int duration = animations_enabled() ? 180 : 0;
  stack_.set_transition_type(Gtk::STACK_TRANSITION_TYPE_CROSSFADE);
  stack_.set_transition_duration(duration);
  stack_.set_homogeneous(false);
  stack_.set_vexpand(true);
  for (const auto& item : nav_) {
    stack_.add(*item.page, item.id);
    item.page->pending_changed.connect(sigc::mem_fun(*this, &MainWindow::update_apply));
  }

  auto* column = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  column->set_margin_top(24);
  column->set_margin_bottom(18);
  column->set_margin_start(24);
  column->set_margin_end(24);
  auto* head = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  head->set_margin_bottom(18);
  heading_icon_.set_pixel_size(48);
  heading_icon_.set_valign(Gtk::ALIGN_CENTER);
  auto* titles = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 3));
  heading_.set_halign(Gtk::ALIGN_START);
  heading_.get_style_context()->add_class("lcos-title");
  summary_.set_halign(Gtk::ALIGN_START);
  summary_.set_xalign(0);
  summary_.set_line_wrap(true);
  summary_.get_style_context()->add_class("lcos-subtitle");
  titles->pack_start(heading_, Gtk::PACK_SHRINK);
  titles->pack_start(summary_, Gtk::PACK_SHRINK);
  head->pack_start(heading_icon_, Gtk::PACK_SHRINK);
  head->pack_start(*titles, Gtk::PACK_EXPAND_WIDGET);
  auto* rule = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
  rule->get_style_context()->add_class("lcos-rule");
  auto* page_head = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  page_head->pack_start(*head, Gtk::PACK_SHRINK);
  page_head->pack_start(*rule, Gtk::PACK_SHRINK);
  page_head_ = page_head;
  column->pack_start(*page_head, Gtk::PACK_SHRINK);

  auto* bar = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  bar->get_style_context()->add_class("lcos-infobar");
  bar->set_margin_top(18);
  auto* bang = Gtk::manage(new Gtk::Label("!"));
  bang->get_style_context()->add_class("lcos-infobar-icon");
  auto* words = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 1));
  auto* bar_title = Gtk::manage(new Gtk::Label("You have unsaved changes"));
  bar_title->set_halign(Gtk::ALIGN_START);
  bar_title->get_style_context()->add_class("lcos-strong");
  infobar_detail_.set_halign(Gtk::ALIGN_START);
  infobar_detail_.set_line_wrap(true);
  infobar_detail_.get_style_context()->add_class("lcos-infobar-detail");
  words->pack_start(*bar_title, Gtk::PACK_SHRINK);
  words->pack_start(infobar_detail_, Gtk::PACK_SHRINK);
  infobar_apply_.get_style_context()->add_class("lcos-primary");
  bar->pack_start(*bang, Gtk::PACK_SHRINK);
  bar->pack_start(*words, Gtk::PACK_EXPAND_WIDGET);
  bar->pack_start(infobar_revert_, Gtk::PACK_SHRINK);
  bar->pack_start(infobar_apply_, Gtk::PACK_SHRINK);
  infobar_reveal_.add(*bar);
  infobar_reveal_.set_transition_type(Gtk::REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
  infobar_reveal_.set_transition_duration(duration);
  infobar_reveal_.set_reveal_child(false);
  column->pack_start(infobar_reveal_, Gtk::PACK_SHRINK);
  column->set_hexpand(true);
  column->set_halign(Gtk::ALIGN_FILL);
  column->pack_start(stack_, Gtk::PACK_EXPAND_WIDGET);
  body->pack_start(*column, Gtk::PACK_EXPAND_WIDGET);
  root->pack_start(*body, Gtk::PACK_EXPAND_WIDGET);

  auto* footer = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  footer->get_style_context()->add_class("lcos-footer");
  status_.set_halign(Gtk::ALIGN_START);
  status_.set_valign(Gtk::ALIGN_CENTER);
  status_.set_hexpand(true);
  status_.get_style_context()->add_class("lcos-faint");
  ok_.get_style_context()->add_class("lcos-primary");
  ok_.set_can_default(true);
  infobar_revert_.get_style_context()->add_class("lcos-action");
  infobar_apply_.get_style_context()->add_class("lcos-action");
  for (Gtk::Button* button : {&ok_, &apply_, &cancel_, &infobar_revert_, &infobar_apply_}) {
    button->set_size_request(86, 28);
    button->set_valign(Gtk::ALIGN_CENTER);
    button->get_style_context()->add_class("lcos-action");
  }
  footer->pack_start(status_, Gtk::PACK_EXPAND_WIDGET);
  footer->pack_start(cancel_, Gtk::PACK_SHRINK);
  footer->pack_start(apply_, Gtk::PACK_SHRINK);
  footer->pack_start(ok_, Gtk::PACK_SHRINK);
  set_default(ok_);
  root->pack_start(*footer, Gtk::PACK_SHRINK);

  search_.signal_search_changed().connect(sigc::mem_fun(*this, &MainWindow::apply_filter));
  search_.signal_activate().connect([this] {
    for (std::size_t i = 0; i < rows_.size() && i < nav_.size(); ++i) {
      if (rows_[i] != nullptr && rows_[i]->get_visible()) {
        show_page(nav_[i].id);
        return;
      }
    }
  });
  sidebar_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
    if (row == nullptr || filtering_ || nav_depth_ > 0) {
      return;
    }
    for (std::size_t i = 0; i < rows_.size(); ++i) {
      if (rows_[i] == row) {
        select_nav(static_cast<int>(i));
        return;
      }
    }
  });
  ok_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::close_keep));
  apply_.signal_clicked().connect([this] { keep_changes(false); });
  cancel_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::close_revert));
  infobar_apply_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::infobar_apply));
  infobar_revert_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::infobar_revert));
  store_.changed.connect([this] { update_apply(); });
  privileged_.changed.connect(sigc::mem_fun(*this, &MainWindow::update_apply));
  signal_size_allocate().connect([this](Gtk::Allocation& allocation) {
    const bool compact = allocation.get_width() > 0 && allocation.get_width() < 860;
    if (compact != compact_) {
      set_compact(compact);
    }
  });

  fit_to_screen();
  const std::string initial = saved_page();
  int start = nav_index(initial.empty() ? "overview" : initial);
  if (start < 0) {
    start = 0;
  }
  show_all();
  // show_all reveals the labels hidden for a narrow screen. Apply that
  // choice again so the icon rail stays collapsed on the first map.
  if (compact_) {
    compact_ = false;
    set_compact(true);
  }
  set_default(ok_);
  if (auto* row = rows_[static_cast<std::size_t>(start)]) {
    sidebar_.select_row(*row);
  }
  select_nav(start);
  update_apply();
  load_icons();
  signal_realize().connect(sigc::mem_fun(*this, &MainWindow::load_icons));
  property_scale_factor().signal_changed().connect(sigc::mem_fun(*this, &MainWindow::load_icons));
  Glib::signal_idle().connect([this]() {
    guard_scroll_tree(*this);
    queue_draw();
    auto screen = get_screen();
    if (screen) {
      const Gdk::Rectangle work = screen->get_monitor_workarea(screen->get_primary_monitor());
      const int max_w = std::max(320, work.get_width() - 8);
      const int max_h = std::max(240, work.get_height() - 8);
      if (get_allocated_width() > max_w || get_allocated_height() > max_h) {
        resize(std::min(std::max(1, get_allocated_width()), max_w),
               std::min(std::max(1, get_allocated_height()), max_h));
      }
    }
    return false;
  });
}

void MainWindow::build_sidebar() {
  std::string section;
  for (const auto& item : nav_) {
    if (item.section != section) {
      section = item.section;
      if (!section.empty()) {
        auto* row = Gtk::manage(new Gtk::ListBoxRow());
        row->set_selectable(false);
        row->set_activatable(false);
        row->set_can_focus(false);
        row->set_size_request(-1, 22);
        row->get_style_context()->add_class("lcos-section");
        auto* label = Gtk::manage(new Gtk::Label());
        std::string small = section;
        for (char& c : small) {
          c = static_cast<char>(g_unichar_tolower(static_cast<gunichar>(static_cast<unsigned char>(c))));
        }
        label->set_markup("<span font_variant='smallcaps' weight='bold' letter_spacing='900'>" +
                          Glib::Markup::escape_text(small) + "</span>");
        label->get_style_context()->add_class("lcos-kicker");
        label->set_halign(Gtk::ALIGN_START);
        label->set_margin_start(10);
        label->set_margin_bottom(3);
        label->set_valign(Gtk::ALIGN_END);
        row->add(*label);
        sidebar_.append(*row);
        section_rows_.push_back(row);
      }
    }
    auto* row = Gtk::manage(new Gtk::ListBoxRow());
    row->get_style_context()->add_class("lcos-nav");
    row->set_can_focus(false);
    auto* line = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 9));
    line->set_margin_top(0);
    line->set_margin_bottom(0);
    line->set_margin_start(8);
    line->set_margin_end(10);
    auto* image = Gtk::manage(new Gtk::Image());
    image->set_pixel_size(24);
    nav_images_.push_back(image);
    auto* label = Gtk::manage(new Gtk::Label(item.title));
    label->set_halign(Gtk::ALIGN_START);
    label->set_valign(Gtk::ALIGN_CENTER);
    label->set_hexpand(true);
    label->set_xalign(0);
    label->set_line_wrap(true);
    label->set_line_wrap_mode(Pango::WRAP_WORD);
    label->set_lines(2);
    label->set_max_width_chars(16);
    label->set_ellipsize(Pango::ELLIPSIZE_END);
    row_labels_.push_back(label);
    auto* dot = Gtk::manage(new Gtk::Label("●"));
    dot->get_style_context()->add_class("lcos-dot");
    dot->set_no_show_all(true);
    row_dots_.push_back(dot);
    line->pack_start(*image, Gtk::PACK_SHRINK);
    line->pack_start(*label, Gtk::PACK_EXPAND_WIDGET);
    line->pack_end(*dot, Gtk::PACK_SHRINK);
    row->add(*line);
    const Glib::ustring tip = item.page->title();
    label->signal_size_allocate().connect([row, label, tip](Gtk::Allocation&) {
      bool ellipsized = false;
      if (auto layout = label->get_layout()) {
        ellipsized = layout->is_ellipsized();
      }
      row->set_has_tooltip(ellipsized);
      if (ellipsized) {
        row->set_tooltip_text(tip);
      }
    });
    sidebar_.append(*row);
    rows_.push_back(row);
  }
  sidebar_.set_can_focus(true);
  search_empty_ = Gtk::manage(new Gtk::Label("No matching settings"));
  search_empty_->set_halign(Gtk::ALIGN_START);
  search_empty_->set_xalign(0);
  search_empty_->set_margin_top(8);
  search_empty_->set_margin_start(10);
  search_empty_->get_style_context()->add_class("lcos-faint");
  search_empty_->set_no_show_all(true);
  search_empty_->hide();
  sidebar_.signal_key_press_event().connect([this](GdkEventKey* event) {
    int delta = 0;
    if (event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_KP_Up) {
      delta = -1;
    } else if (event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_KP_Down) {
      delta = 1;
    } else if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter ||
               event->keyval == GDK_KEY_space) {
      return true;
    } else {
      return false;
    }
    if (rows_.empty()) {
      return true;
    }
    int index = current_index_ < 0 ? 0 : current_index_;
    for (int step = 0; step < static_cast<int>(rows_.size()); ++step) {
      index = (index + delta + static_cast<int>(rows_.size())) % static_cast<int>(rows_.size());
      if (index < 0) {
        continue;
      }
      auto* candidate = rows_[static_cast<std::size_t>(index)];
      if (candidate != nullptr && candidate->get_visible() && candidate->get_selectable()) {
        sidebar_.select_row(*candidate);
        break;
      }
    }
    return true;
  });
}

void MainWindow::load_icons() {
  const int scale = std::max(1, get_scale_factor());
  for (std::size_t i = 0; i < nav_images_.size() && i < nav_.size(); ++i) {
    if (auto pixbuf = load_named_icon(nav_[i].icon, 24, scale)) {
      nav_images_[i]->set(Gdk::Cairo::create_surface_from_pixbuf(pixbuf, scale));
    }
  }
  if (auto find = load_named_icon("edit-find", 16, scale)) {
    search_.set_icon_from_pixbuf(find, Gtk::ENTRY_ICON_PRIMARY);
  }
  if (current_index_ >= 0 && current_index_ < static_cast<int>(nav_.size())) {
    if (auto pixbuf = load_named_icon(nav_[static_cast<std::size_t>(current_index_)].icon, 48, scale)) {
      heading_icon_.set(Gdk::Cairo::create_surface_from_pixbuf(pixbuf, scale));
    }
  }
}

int MainWindow::nav_index(const std::string& id) const {
  for (std::size_t i = 0; i < nav_.size(); ++i) {
    if (nav_[i].id == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void MainWindow::show_page(const std::string& id) {
  const int index = nav_index(id);
  if (index < 0) {
    return;
  }
  if (nav_depth_ > 0) {
    queued_page_ = index;
    return;
  }
  if (auto* row = rows_[static_cast<std::size_t>(index)]) {
    sidebar_.select_row(*row);
  }
  select_nav(index);
  // A page that is still inside on_page_shown (locale listing, a nested
  // polkit loop) can put the previous page back. Apply the request again
  // once that handler has returned.
  Glib::signal_idle().connect([this, index] {
    if (index < 0 || index >= static_cast<int>(nav_.size())) {
      return false;
    }
    const bool showing = current_index_ == index &&
                         stack_.get_visible_child() == nav_[static_cast<std::size_t>(index)].page;
    const bool row_ok = rows_[static_cast<std::size_t>(index)] == nullptr ||
                        sidebar_.get_selected_row() == rows_[static_cast<std::size_t>(index)];
    if (showing && row_ok) {
      return false;
    }
    if (auto* row = rows_[static_cast<std::size_t>(index)]) {
      sidebar_.select_row(*row);
    }
    const int saved = current_index_;
    current_index_ = -1;
    select_nav(index);
    if (current_index_ < 0) {
      current_index_ = saved;
    }
    return false;
  });
}

void MainWindow::mark_page(const std::string& id) {
  if (!id.empty()) {
    dirty_ids_.insert(id);
  }
  update_apply();
}

void MainWindow::select_nav(int index) {
  if (index < 0 || index >= static_cast<int>(nav_.size())) {
    return;
  }
  if (nav_depth_ > 0) {
    return;
  }
  ++nav_depth_;
  if (current_index_ == index && stack_.get_visible_child() == nav_[static_cast<std::size_t>(index)].page) {
    store_.set_edit_page(nav_[static_cast<std::size_t>(index)].id);
    update_apply();
  } else {
  current_index_ = index;
  auto& item = nav_[static_cast<std::size_t>(index)];
  store_.set_edit_page(item.id);
  stack_.set_visible_child(*item.page);
  item.page->queue_draw();
  const bool overview = item.id == "overview";
  heading_.set_text(overview ? "" : item.page->title());
  summary_.set_text(overview ? "" : item.page->summary());
  heading_.set_visible(!overview);
  summary_.set_visible(!overview && !item.page->summary().empty());
  heading_icon_.set_visible(!overview);
  if (page_head_ != nullptr) {
    page_head_->set_visible(!overview);
  }
  load_icons();
  item.page->on_page_shown();
  item.page->align_labels();
  guard_scroll_tree(*item.page);
  if (!suppress_save_ && dump_dir_.empty()) {
    remember_page(item.id);
  }
  update_apply();
  }
  --nav_depth_;
  if (queued_page_ >= 0) {
    const int next = queued_page_;
    queued_page_ = -1;
    show_page(nav_[static_cast<std::size_t>(next)].id);
  }
}

void MainWindow::apply_filter() {
  const Glib::ustring query = search_.get_text().lowercase();
  filtering_ = true;
  std::string section;
  Gtk::ListBoxRow* section_row = nullptr;
  bool section_visible = false;
  std::size_t section_slot = 0;
  auto flush = [&]() {
    if (section_row != nullptr) {
      section_row->set_visible(section_visible && !compact_);
    }
  };
  for (std::size_t i = 0; i < nav_.size(); ++i) {
    if (nav_[i].section != section) {
      flush();
      section = nav_[i].section;
      section_row = section.empty() || section_slot >= section_rows_.size()
                        ? nullptr
                        : section_rows_[section_slot++];
      section_visible = false;
    }
    Glib::ustring hay = Glib::ustring(nav_[i].title + " " + nav_[i].page->title() + " " +
                                      nav_[i].page->summary() + " " + nav_[i].keywords)
                            .lowercase();
    const bool match = query.empty() || hay.find(query) != Glib::ustring::npos;
    rows_[i]->set_visible(match);
    if (match) {
      section_visible = true;
    }
  }
  flush();
  filtering_ = false;
  if (search_empty_ != nullptr) {
    bool any = query.empty();
    if (!any) {
      for (auto* row : rows_) {
        if (row != nullptr && row->get_visible()) {
          any = true;
          break;
        }
      }
    }
    search_empty_->set_visible(!query.empty() && !any);
  }
}

void MainWindow::set_compact(bool compact) {
  compact_ = compact;
  side_scroll_.set_size_request(-1, -1);
  if (auto* chrome = dynamic_cast<SideChrome*>(side_chrome_)) {
    chrome->set_wide(!compact);
  }
  for (auto* label : row_labels_) {
    label->set_visible(!compact);
  }
  for (auto* row : section_rows_) {
    row->set_visible(!compact);
  }
  search_.set_visible(!compact);
  if (!compact) {
    apply_filter();
  }
}

void MainWindow::fit_to_screen() {
  auto screen = get_screen();
  if (!screen) {
    set_default_size(1100, 720);
    return;
  }
  const int monitor = screen->get_primary_monitor();
  const Gdk::Rectangle work = screen->get_monitor_workarea(monitor);
  const int max_w = std::max(320, work.get_width() - 8);
  const int max_h = std::max(240, work.get_height() - 8);
  // One minimum on every page. A smaller screen still gets a window that
  // fits, and the sidebar collapses to icons below 860 px.
  Gdk::Geometry hints;
  hints.min_width = std::min(kWindowMinWidth, max_w);
  hints.min_height = std::min(kWindowMinHeight, max_h);
  set_geometry_hints(*this, hints, Gdk::HINT_MIN_SIZE);
  const int width = std::min(1100, max_w);
  const int height = std::min(720, max_h);
  set_default_size(width, height);
  if (width < 860) {
    set_compact(true);
  }
}

std::string MainWindow::saved_page() const {
  Glib::KeyFile keys;
  try {
    if (!keys.load_from_file(state_path())) {
      return {};
    }
    return keys.get_string("window", "last-page");
  } catch (const Glib::Error&) {
    return {};
  }
}

void MainWindow::remember_page(const std::string& id) {
  if (id.empty()) {
    return;
  }
  const std::string path = state_path();
  const std::string dir = Glib::path_get_dirname(path);
  g_mkdir_with_parents(dir.c_str(), 0700);
  Glib::KeyFile keys;
  try {
    keys.load_from_file(path);
  } catch (const Glib::Error&) {
  }
  keys.set_string("window", "last-page", id);
  try {
    Glib::file_set_contents(path, keys.to_data());
  } catch (const Glib::Error&) {
  }
}

bool MainWindow::page_privileged(const NavItem& item) const {
  if (item.id == "overview") {
    return privileged_.action_open("set-hostname");
  }
  if (item.id == "date-time") {
    return privileged_.action_open("set-timezone") || privileged_.action_open("set-time") ||
           privileged_.action_open("set-ntp");
  }
  if (item.priv == PrivKind::Accounts) {
    return privileged_.accounts_dirty();
  }
  if (item.priv == PrivKind::Locale) {
    return privileged_.locale_dirty();
  }
  return false;
}

void MainWindow::sync_dirty_from_store() {
  dirty_ids_.clear();
  for (const auto& id : store_.dirty_pages()) {
    if (!id.empty() && id != "overview") {
      dirty_ids_.insert(id);
    }
  }
}

int MainWindow::pending_total() const {
  int count = store_.pending_count() + privileged_.open_action_count();
  for (const auto& item : nav_) {
    count += item.page->unsaved_count();
  }
  return count;
}

MainWindow::CloseChoice MainWindow::confirm_close() {
  const int count = pending_total();
  if (count <= 0) {
    return CloseChoice::Discard;
  }
  const std::string title =
      count == 1 ? "Discard 1 change?" : "Discard " + std::to_string(count) + " changes?";
  Gtk::MessageDialog dialog(*this, title, false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
  dialog.set_title("LCOS Command Center");
  dialog.set_secondary_text("Cancel restores changes. OK and Apply keep them.");
  dialog.add_button("Keep editing", Gtk::RESPONSE_CANCEL);
  dialog.add_button("Discard", Gtk::RESPONSE_REJECT);
  dialog.add_button("Apply and close", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_CANCEL);
  const int response = dialog.run();
  if (response == Gtk::RESPONSE_OK) {
    return CloseChoice::Apply;
  }
  if (response == Gtk::RESPONSE_REJECT) {
    return CloseChoice::Discard;
  }
  return CloseChoice::Stay;
}

bool MainWindow::on_key_press_event(GdkEventKey* event) {
  const bool control = event != nullptr && (event->state & GDK_CONTROL_MASK) != 0;
  if (control && (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
    if (search_.get_visible()) {
      search_.grab_focus();
      return true;
    }
  }
  return Gtk::ApplicationWindow::on_key_press_event(event);
}

bool MainWindow::page_marked(const NavItem& item) const {
  return dirty_ids_.count(item.id) != 0 && store_.session_dirty();
}

void MainWindow::update_apply() {
  sync_dirty_from_store();
  int count = 0;
  bool any_local = false;
  for (std::size_t i = 0; i < nav_.size(); ++i) {
    const int local = nav_[i].page->unsaved_count();
    const bool marked = page_marked(nav_[i]);
    const bool priv = page_privileged(nav_[i]);
    if (local > 0) {
      count += local;
      any_local = true;
    } else if (marked || priv) {
      ++count;
    }
    if (i < row_dots_.size()) {
      row_dots_[i]->set_visible(local > 0 || marked || priv);
    }
  }
  if (privileged_.dirty() && !any_local) {
    bool counted = false;
    for (const auto& item : nav_) {
      if (page_privileged(item)) {
        counted = true;
        break;
      }
    }
    if (!counted) {
      ++count;
    }
  }
  const bool overview = current_index_ >= 0 && nav_[static_cast<std::size_t>(current_index_)].id == "overview";
  const int changes = pending_total();
  if (changes <= 0 && overview) {
    status_.set_text("Quick settings apply right away.");
    ok_.hide();
    apply_.hide();
    cancel_.set_label("Close");
  } else {
    if (changes <= 0) {
      status_.set_text("Cancel restores changes. OK and Apply keep them.");
    } else if (changes == 1) {
      status_.set_text("●  1 unsaved change. Cancel restores it. OK and Apply keep it.");
    } else {
      status_.set_text("●  " + std::to_string(changes) +
                       " unsaved changes. Cancel restores them. OK and Apply keep them.");
    }
    status_.get_style_context()->add_class("lcos-faint");
    ok_.show();
    apply_.show();
    cancel_.set_label(changes <= 0 && overview ? "Close" : "Cancel");
  }
  bool pending = privileged_.dirty() || store_.session_dirty() || count > 0;
  for (const auto& item : nav_) {
    pending = pending || item.page->has_pending();
  }
  apply_.set_sensitive(pending);
  if (!store_.available() && count <= 0 && !overview) {
    status_.set_text("Settings service is not available. Changes cannot be saved.");
  }

  std::string detail;
  bool can_revert = false;
  bool can_apply = false;
  if (current_index_ >= 0) {
    auto* page = nav_[static_cast<std::size_t>(current_index_)].page;
    detail = page->pending_detail();
    can_revert = page->has_pending();
    can_apply = page->has_pending();
    if (detail.empty() && page_marked(nav_[static_cast<std::size_t>(current_index_)])) {
      detail = "Cancel restores changes. OK and Apply keep them.";
      can_apply = true;
      can_revert = true;
    } else if (detail.empty() && page_privileged(nav_[static_cast<std::size_t>(current_index_)])) {
      detail = "Cancel restores changes. OK and Apply keep them.";
      can_apply = true;
      can_revert = true;
    }
  }
  infobar_detail_.set_text(detail);
  infobar_reveal_.set_reveal_child(!detail.empty());
  infobar_revert_.set_sensitive(can_revert);
  infobar_apply_.set_sensitive(can_apply || page_marked(current_index_ >= 0 ? nav_[static_cast<std::size_t>(current_index_)] : nav_.front()));
}

void MainWindow::infobar_revert() {
  if (current_index_ < 0) {
    return;
  }
  auto& item = nav_[static_cast<std::size_t>(current_index_)];
  item.page->discard_pending();
  if (store_.page_dirty(item.id)) {
    store_.revert_page(item.id);
  }
  item.page->reload_from_store();
  dirty_ids_.erase(item.id);
  update_apply();
}

void MainWindow::infobar_apply() {
  if (current_index_ < 0) {
    return;
  }
  auto& item = nav_[static_cast<std::size_t>(current_index_)];
  if (item.page->has_pending() && !item.page->commit_pending()) {
    update_apply();
    return;
  }
  if (store_.page_dirty(item.id)) {
    store_.drop_page(item.id);
  }
  dirty_ids_.erase(item.id);
  update_apply();
}

void MainWindow::tell(const std::string& message) {
  std::string primary = message;
  std::string secondary;
  const auto nl = message.find('\n');
  if (nl != std::string::npos) {
    primary = message.substr(0, nl);
    secondary = message.substr(nl + 1);
  }
  Gtk::MessageDialog dialog(*this, primary, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
  if (!secondary.empty()) {
    dialog.set_secondary_text(secondary);
  }
  dialog.run();
}

void MainWindow::keep_changes(bool close) {
  if (closing_) {
    return;
  }
  for (const auto& item : nav_) {
    if (item.page->has_pending() && !item.page->commit_pending()) {
      update_apply();
      return;
    }
  }
  if (privileged_.busy()) {
    privileged_.wait_until_idle();
  }
  const PrivResult kept = close ? privileged_.commit() : privileged_.checkpoint();
  if (!kept.ok) {
    tell(kept.error.empty() ? "The privileged changes could not be kept." : kept.error);
    update_apply();
    return;
  }
  store_.drop_originals();
  dirty_ids_.clear();
  if (close) {
    closing_ = true;
    hide();
    return;
  }
  update_apply();
}

void MainWindow::close_keep() { keep_changes(true); }

void MainWindow::close_revert() {
  if (closing_) {
    return;
  }
  const CloseChoice choice = confirm_close();
  if (choice == CloseChoice::Stay) {
    return;
  }
  if (choice == CloseChoice::Apply) {
    close_keep();
    return;
  }
  revert_and_close();
}

void MainWindow::revert_and_close() {
  if (closing_) {
    return;
  }
  if (privileged_.busy()) {
    privileged_.cancel();
    privileged_.wait_until_idle();
  }
  const PrivResult reverted = privileged_.revert();
  if (!reverted.ok) {
    tell(reverted.error.empty() ? "The privileged changes could not be restored." : reverted.error);
    return;
  }
  const bool displays = store_.channel_touched("displays");
  DisplayPage* display = nullptr;
  for (const auto& item : nav_) {
    if (display == nullptr) {
      display = dynamic_cast<DisplayPage*>(item.page);
    }
    item.page->revert_page();
  }
  const bool uncommitted_display = display != nullptr && display->uncommitted_hardware();
  store_.revert_session();
  if (displays && !uncommitted_display) {
    store_.set_string_untracked("displays", "/Schemes/Apply", "Default");
  }
  if (display != nullptr) {
    const std::string restored = display->restore_session_hardware();
    if (!restored.empty()) {
      tell(restored);
      update_apply();
      return;
    }
  }
  dirty_ids_.clear();
  closing_ = true;
  hide();
}

bool MainWindow::on_delete_event(GdkEventAny*) {
  close_revert();
  return true;
}

void MainWindow::dump_pages(const std::string& directory) {
  dump_dir_ = directory;
  suppress_save_ = true;
  shots_.clear();
  stack_.set_transition_duration(0);
  if (const char* spec = g_getenv("LCOS_SETTINGS_DUMP_SIZE")) {
    int width = 1100;
    int height = 720;
    if (std::sscanf(spec, "%dx%d", &width, &height) == 2 && width > 0 && height > 0) {
      for (int page = 0; page < static_cast<int>(nav_.size()); ++page) {
        Shot shot;
        shot.page = page;
        shot.name = nav_[static_cast<std::size_t>(page)].id + std::string("-") + spec;
        shot.width = width;
        shot.height = height;
        shots_.push_back(shot);
      }
      shot_index_ = 0;
      dump_phase_ = 0;
      resize(width, height);
      Glib::signal_timeout().connect(sigc::mem_fun(*this, &MainWindow::dump_step), 280);
      return;
    }
  }
  if (g_getenv("LCOS_SETTINGS_DUMP_FAST") != nullptr) {
    const int sizes[][2] = {{1100, 720}, {1000, 550}};
    for (const auto& size : sizes) {
      for (int page = 0; page < static_cast<int>(nav_.size()); ++page) {
        Shot shot;
        shot.page = page;
        shot.frame = 0;
        shot.name = nav_[static_cast<std::size_t>(page)].id + std::string("-") +
                    std::to_string(size[0]) + "x" + std::to_string(size[1]);
        shot.width = size[0];
        shot.height = size[1];
        shots_.push_back(shot);
      }
      Shot keep;
      keep.page = std::max(0, nav_index("display"));
      keep.name = std::string("display-keep-") + std::to_string(size[0]) + "x" +
                  std::to_string(size[1]);
      keep.width = size[0];
      keep.height = size[1];
      keep.keep = true;
      shots_.push_back(keep);
    }
    shot_index_ = 0;
    dump_phase_ = 0;
    resize(1100, 720);
    Glib::signal_timeout().connect(sigc::mem_fun(*this, &MainWindow::dump_step), 350);
    return;
  }
  for (int page = 0; page < static_cast<int>(nav_.size()); ++page) {
    const int frames = std::max(1, nav_[static_cast<std::size_t>(page)].page->screenshot_count());
    for (int frame = 0; frame < frames; ++frame) {
      Shot shot;
      shot.page = page;
      shot.frame = frame;
      shot.name = nav_[static_cast<std::size_t>(page)].id;
      const std::string suffix = nav_[static_cast<std::size_t>(page)].page->screenshot_suffix(frame);
      if (!suffix.empty()) {
        shot.name += "-" + suffix;
      }
      shot.width = 1100;
      shot.height = 720;
      shots_.push_back(shot);
    }
  }
  const int overview_index = std::max(0, nav_index("overview"));
  const int pin_sizes[][2] = {{900, 600}, {1100, 720}, {1400, 900}};
  for (const auto& size : pin_sizes) {
    Shot pinned;
    pinned.page = overview_index;
    pinned.name = std::string("sidebar-overview-") + std::to_string(size[0]) + "x" +
                  std::to_string(size[1]);
    pinned.width = size[0];
    pinned.height = size[1];
    shots_.push_back(pinned);
  }
  const int overview = overview_index;
  const int display = std::max(0, nav_index("display"));
  const int appearance = std::max(0, nav_index("appearance"));
  const int users = std::max(0, nav_index("users"));
  const int sizes[][2] = {{900, 600}, {1100, 720}, {1400, 900}};
  for (const auto& size : sizes) {
    for (int page : {overview, display, appearance, users}) {
      Shot shot;
      shot.page = page;
      shot.frame = 0;
      shot.name = std::string("redesign-") + nav_[static_cast<std::size_t>(page)].id + "-" +
                  std::to_string(size[0]) + "x" + std::to_string(size[1]);
      shot.width = size[0];
      shot.height = size[1];
      shots_.push_back(shot);
    }
    Shot unsaved;
    unsaved.page = users;
    unsaved.name = std::string("redesign-users-unsaved-") + std::to_string(size[0]) + "x" +
                   std::to_string(size[1]);
    unsaved.width = size[0];
    unsaved.height = size[1];
    unsaved.unsaved = true;
    shots_.push_back(unsaved);
    Shot keep;
    keep.page = display;
    keep.name = std::string("redesign-display-keep-") + std::to_string(size[0]) + "x" +
                std::to_string(size[1]);
    keep.width = size[0];
    keep.height = size[1];
    keep.keep = true;
    shots_.push_back(keep);
  }
  Shot compact;
  compact.page = static_cast<int>(nav_.size()) - 1;
  compact.frame = 1;
  compact.name = "window-short";
  compact.width = 900;
  compact.height = 600;
  shots_.push_back(compact);
  shot_index_ = 0;
  dump_phase_ = 0;
  resize(1100, 720);
  Glib::signal_timeout().connect(sigc::mem_fun(*this, &MainWindow::dump_step), 400);
}

bool MainWindow::dump_step() {
  if (shot_index_ >= static_cast<int>(shots_.size())) {
    hide();
    return false;
  }
  const Shot& shot = shots_[static_cast<std::size_t>(shot_index_)];
  if (dump_phase_ == 0) {
    if (shot.page >= 0 && shot.page < static_cast<int>(rows_.size()) && rows_[static_cast<std::size_t>(shot.page)]) {
      sidebar_.select_row(*rows_[static_cast<std::size_t>(shot.page)]);
    }
    if (shot.width > 0 && shot.height > 0) {
      resize(shot.width, shot.height);
    } else {
      resize(1100, 720);
    }
    select_nav(shot.page);
    if (shot.width <= 0) {
      resize(1100, 720);
    }
    nav_[static_cast<std::size_t>(shot.page)].page->prepare_screenshot(shot.frame);
    if (shot.unsaved) {
      if (auto* users = dynamic_cast<UsersPage*>(nav_[static_cast<std::size_t>(shot.page)].page)) {
        users->stage_screenshot_edit();
      }
    }
    if (shot.keep) {
      if (auto* display = dynamic_cast<DisplayPage*>(nav_[static_cast<std::size_t>(shot.page)].page)) {
        display->open_keep_preview();
      }
    }
    nav_[static_cast<std::size_t>(shot.page)].page->queue_draw();
    update_apply();
    dump_phase_ = 1;
    return true;
  }
  while (g_main_context_pending(nullptr)) {
    g_main_context_iteration(nullptr, FALSE);
  }
  queue_draw();
  if (auto gdk_window = get_window()) {
    gdk_window->process_updates(true);
  }
  auto window = get_window();
  if (window) {
    const int width = std::max(1, get_allocated_width());
    const int height = std::max(1, get_allocated_height());
    try {
      int origin_x = 0;
      int origin_y = 0;
      window->get_origin(origin_x, origin_y);
      auto root = window->get_display()->get_default_screen()->get_root_window();
      auto pixbuf = Gdk::Pixbuf::create(root, origin_x, origin_y, width, height);
      const std::string path = dump_dir_ + "/" + shot.name + ".png";
      pixbuf->save(path, "png");
      std::cerr << "wrote " << path << "\n";
    } catch (const Glib::Error& error) {
      std::cerr << "screenshot failed: " << error.what() << "\n";
    }
  }
  if (shot.keep) {
    if (auto* display = dynamic_cast<DisplayPage*>(nav_[static_cast<std::size_t>(shot.page)].page)) {
      display->close_keep_preview();
    }
  }
  dump_phase_ = 0;
  ++shot_index_;
  if (shot_index_ >= static_cast<int>(shots_.size())) {
    hide();
    return false;
  }
  return true;
}

}  // namespace lcos
