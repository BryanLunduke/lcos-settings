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
#include <iostream>

namespace lcos {
namespace {

class WidthClamp : public Gtk::Bin {
 public:
  WidthClamp() { set_hexpand(true); }
  void set_child(Gtk::Widget& child) { add(child); }

 protected:
  Gtk::SizeRequestMode get_request_mode_vfunc() const override { return Gtk::SIZE_REQUEST_HEIGHT_FOR_WIDTH; }
  void get_preferred_width_vfunc(int& minimum, int& natural) const override {
    minimum = 280;
    natural = 820;
    if (get_child() != nullptr) {
      int child_min = 0;
      int child_nat = 0;
      get_child()->get_preferred_width(child_min, child_nat);
      minimum = std::min(minimum, std::max(0, child_min));
    }
  }
  void on_size_allocate(Gtk::Allocation& allocation) override {
    set_allocation(allocation);
    Gtk::Widget* child = get_child();
    if (child == nullptr || !child->get_visible()) {
      return;
    }
    const int width = std::min(820, allocation.get_width());
    Gtk::Allocation inner;
    inner.set_x(allocation.get_x() + std::max(0, (allocation.get_width() - width) / 2));
    inner.set_y(allocation.get_y());
    inner.set_width(width);
    inner.set_height(allocation.get_height());
    child->size_allocate(inner);
  }
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
      *appearance, *sound, *users, pulse_,
      [this](const std::string& id) { show_page(id); },
      [this](const std::string& id) { mark_page(id); }));

  nav_ = {
      {"overview", "Overview", "computer", "system version hostname memory processor graphics storage quick settings dark style volume", "", PrivKind::Off, overview},
      {"appearance", "Appearance", "preferences-desktop-theme", "style theme icons font dpi antialias hinting toolbar scaling", "Look & Feel", PrivKind::Off, appearance},
      {"desktop", "Desktop", "preferences-desktop-wallpaper", "wallpaper background icons menus", "Look & Feel", PrivKind::Off, desktop},
      {"window-manager", "Window Manager", "preferences-system-windows", "focus shortcuts title bar theme placement", "Look & Feel", PrivKind::Off, windows},
      {"window-tweaks", "WM Tweaks", "preferences-system", "compositor cycling focus accessibility zoom placement tweaks", "Look & Feel", PrivKind::Off, tweaks},
      {"notifications", "Notifications", "preferences-system-notifications", "notify theme position log applications", "Look & Feel", PrivKind::Off, notifications},
      {"display", "Display", "preferences-desktop-display", "resolution refresh scale rotation mirror extend monitor primary reflection", "Hardware", PrivKind::Off, display},
      {"sound", "Sound", "audio-volume-high", "volume mute output input microphone profile", "Hardware", PrivKind::Off, sound},
      {"power", "Power", "battery", "battery sleep lid blank brightness lock", "Hardware", PrivKind::Off, power},
      {"keyboard", "Keyboard", "preferences-desktop-keyboard", "layout repeat shortcut model compose", "Hardware", PrivKind::Off, keyboard},
      {"mouse", "Mouse", "input-mouse", "pointer touchpad scrolling cursor speed", "Hardware", PrivKind::Off, mouse},
      {"removable-media", "Removable Drives and Media", "drive-removable-media", "usb camera mount cd dvd printer tablet", "Hardware", PrivKind::Off, media},
      {"date-time", "Date & Time", "appointment-soon", "clock timezone ntp calendar", "System", PrivKind::System, datetime},
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
  side_scroll_.set_size_request(318, -1);
  side_scroll_.set_min_content_width(318);
  sidebar_.get_style_context()->add_class("lcos-sidebar");
  sidebar_.set_selection_mode(Gtk::SELECTION_BROWSE);
  auto* side = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  side->set_margin_top(0);
  side->set_margin_start(8);
  side->set_margin_end(4);
  side->set_margin_bottom(6);
  search_.get_style_context()->add_class("lcos-search");
  search_.set_placeholder_text("Search settings");
  search_.set_margin_top(8);
  search_.set_margin_start(10);
  search_.set_margin_end(10);
  search_.set_margin_bottom(4);
  auto* side_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  side_box->get_style_context()->add_class("lcos-sidebar");
  side_box->pack_start(search_, Gtk::PACK_SHRINK);
  side->set_margin_top(0);
  build_sidebar();
  side->pack_start(sidebar_, Gtk::PACK_EXPAND_WIDGET);
  side_scroll_.add(*side);
  side_box->pack_start(side_scroll_, Gtk::PACK_EXPAND_WIDGET);
  side_box->set_size_request(318, -1);
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
  column->set_margin_top(18);
  column->set_margin_bottom(12);
  column->set_margin_start(12);
  column->set_margin_end(12);
  auto* head = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  head->set_margin_start(12);
  head->set_margin_end(12);
  head->set_margin_bottom(18);
  heading_icon_.set_pixel_size(48);
  heading_icon_.set_valign(Gtk::ALIGN_CENTER);
  auto* titles = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 3));
  heading_.set_halign(Gtk::ALIGN_START);
  heading_.get_style_context()->add_class("lcos-title");
  summary_.set_halign(Gtk::ALIGN_START);
  summary_.set_line_wrap(true);
  summary_.get_style_context()->add_class("lcos-subtitle");
  titles->pack_start(heading_, Gtk::PACK_SHRINK);
  titles->pack_start(summary_, Gtk::PACK_SHRINK);
  head->pack_start(heading_icon_, Gtk::PACK_SHRINK);
  head->pack_start(*titles, Gtk::PACK_EXPAND_WIDGET);
  column->pack_start(*head, Gtk::PACK_SHRINK);
  auto* rule = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
  rule->set_margin_start(12);
  rule->set_margin_end(12);
  column->pack_start(*rule, Gtk::PACK_SHRINK);

  auto* bar = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  bar->get_style_context()->add_class("lcos-infobar");
  bar->set_margin_top(18);
  bar->set_margin_start(12);
  bar->set_margin_end(12);
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
  column->pack_start(stack_, Gtk::PACK_EXPAND_WIDGET);

  auto* clamp = Gtk::manage(new WidthClamp());
  clamp->set_child(*column);
  body->pack_start(*clamp, Gtk::PACK_EXPAND_WIDGET);
  root->pack_start(*body, Gtk::PACK_EXPAND_WIDGET);

  auto* footer = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  footer->get_style_context()->add_class("lcos-footer");
  footer->set_margin_start(24);
  footer->set_margin_end(24);
  footer->set_margin_top(8);
  footer->set_margin_bottom(8);
  status_.set_halign(Gtk::ALIGN_START);
  status_.set_hexpand(true);
  status_.get_style_context()->add_class("lcos-faint");
  ok_.get_style_context()->add_class("lcos-primary");
  ok_.set_can_default(true);
  for (Gtk::Button* button : {&ok_, &apply_, &cancel_}) {
    button->set_size_request(86, 28);
    button->get_style_context()->add_class("lcos-action");
  }
  footer->pack_start(status_, Gtk::PACK_EXPAND_WIDGET);
  footer->pack_start(ok_, Gtk::PACK_SHRINK);
  footer->pack_start(apply_, Gtk::PACK_SHRINK);
  footer->pack_start(cancel_, Gtk::PACK_SHRINK);
  root->pack_start(*footer, Gtk::PACK_SHRINK);

  search_.signal_search_changed().connect(sigc::mem_fun(*this, &MainWindow::apply_filter));
  sidebar_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
    if (row == nullptr || filtering_) {
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
  store_.changed.connect([this] {
    if (!store_.session_dirty()) {
      dirty_ids_.clear();
    } else if (current_index_ >= 0) {
      const auto& item = nav_[static_cast<std::size_t>(current_index_)];
      if (item.id != "overview") {
        dirty_ids_.insert(item.id);
      }
    }
    update_apply();
  });
  privileged_.changed.connect(sigc::mem_fun(*this, &MainWindow::update_apply));
  signal_size_allocate().connect([this](Gtk::Allocation& allocation) {
    const bool compact = allocation.get_width() > 0 && allocation.get_width() < 900;
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
  cancel_.grab_focus();
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
        row->set_size_request(-1, 18);
        row->get_style_context()->add_class("lcos-section");
        auto* label = Gtk::manage(new Gtk::Label());
        std::string small = section;
        for (char& c : small) {
          c = static_cast<char>(g_unichar_tolower(static_cast<gunichar>(static_cast<unsigned char>(c))));
        }
        label->set_markup(
            "<span font_variant='smallcaps' weight='bold' color='#7c776f' letter_spacing='1400'>" +
            Glib::Markup::escape_text(small) + "</span>");
        label->set_halign(Gtk::ALIGN_START);
        label->set_margin_start(8);
        label->set_margin_top(2);
        row->add(*label);
        sidebar_.append(*row);
        section_rows_.push_back(row);
      }
    }
    auto* row = Gtk::manage(new Gtk::ListBoxRow());
    row->get_style_context()->add_class("lcos-nav");
    row->set_size_request(-1, 26);
    row->set_tooltip_text(item.page->title());
    auto* line = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
    line->set_margin_top(0);
    line->set_margin_bottom(0);
    line->set_margin_start(6);
    line->set_margin_end(6);
    auto* image = Gtk::manage(new Gtk::Image());
    image->set_pixel_size(24);
    nav_images_.push_back(image);
    auto* label = Gtk::manage(new Gtk::Label(item.title));
    label->set_halign(Gtk::ALIGN_START);
    label->set_hexpand(true);
    label->set_ellipsize(Pango::ELLIPSIZE_NONE);
    row_labels_.push_back(label);
    auto* dot = Gtk::manage(new Gtk::Label("●"));
    dot->get_style_context()->add_class("lcos-dot");
    dot->set_no_show_all(true);
    row_dots_.push_back(dot);
    line->pack_start(*image, Gtk::PACK_SHRINK);
    line->pack_start(*label, Gtk::PACK_EXPAND_WIDGET);
    line->pack_end(*dot, Gtk::PACK_SHRINK);
    row->add(*line);
    sidebar_.append(*row);
    rows_.push_back(row);
  }
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
  if (auto* row = rows_[static_cast<std::size_t>(index)]) {
    sidebar_.select_row(*row);
  }
  select_nav(index);
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
  if (current_index_ == index && stack_.get_visible_child() == nav_[static_cast<std::size_t>(index)].page) {
    update_apply();
    return;
  }
  current_index_ = index;
  auto& item = nav_[static_cast<std::size_t>(index)];
  stack_.set_visible_child(*item.page);
  item.page->queue_draw();
  const bool overview = item.id == "overview";
  heading_.set_text(overview ? "" : item.page->title());
  summary_.set_text(overview ? "" : item.page->summary());
  heading_.set_visible(!overview);
  summary_.set_visible(!overview && !item.page->summary().empty());
  heading_icon_.set_visible(!overview);
  load_icons();
  item.page->on_page_shown();
  item.page->align_labels();
  guard_scroll_tree(*item.page);
  if (!suppress_save_ && dump_dir_.empty()) {
    remember_page(item.id);
  }
  update_apply();
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
}

void MainWindow::set_compact(bool compact) {
  compact_ = compact;
  side_scroll_.set_size_request(compact ? 64 : 318, -1);
  if (side_chrome_ != nullptr) {
    side_chrome_->set_size_request(compact ? 64 : 318, -1);
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
  Gdk::Geometry hints;
  hints.min_width = std::min(640, max_w);
  hints.min_height = std::min(400, max_h);
  set_geometry_hints(*this, hints, Gdk::HINT_MIN_SIZE);
  set_default_size(std::min(1100, max_w), std::min(720, max_h));
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
  if (item.priv == PrivKind::Accounts) {
    return privileged_.accounts_dirty();
  }
  if (item.priv == PrivKind::Locale) {
    return privileged_.locale_dirty();
  }
  if (item.priv == PrivKind::System) {
    return privileged_.system_dirty();
  }
  return false;
}

bool MainWindow::page_marked(const NavItem& item) const {
  return dirty_ids_.count(item.id) != 0 && store_.session_dirty();
}

void MainWindow::update_apply() {
  if (!store_.session_dirty()) {
    dirty_ids_.clear();
  }
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
  if (count <= 0 && overview) {
    status_.set_text("Quick settings apply right away.");
    ok_.hide();
    apply_.hide();
    cancel_.set_label("Close");
  } else {
    if (count <= 0) {
      status_.set_text("");
    } else if (count == 1) {
      status_.set_markup("<span foreground=\"#e08a2c\">●</span>  1 unsaved change");
    } else {
      status_.set_markup("<span foreground=\"#e08a2c\">●</span>  " + std::to_string(count) +
                         " unsaved changes");
    }
    ok_.show();
    apply_.show();
    cancel_.set_label("Cancel");
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
      detail = "Changes on this page have not been kept.";
      can_apply = true;
      can_revert = true;
    } else if (detail.empty() && page_privileged(nav_[static_cast<std::size_t>(current_index_)])) {
      detail = "A change on this page has not been kept. Cancel can restore it.";
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
  if (item.page->discard_pending()) {
    dirty_ids_.erase(item.id);
    update_apply();
    return;
  }
  if (page_marked(item)) {
    store_.revert_session();
    dirty_ids_.clear();
    if (auto* appearance = dynamic_cast<AppearancePage*>(item.page)) {
      appearance->reload_from_store();
    }
    update_apply();
  }
}

void MainWindow::infobar_apply() {
  if (current_index_ < 0) {
    return;
  }
  auto* page = nav_[static_cast<std::size_t>(current_index_)].page;
  if (page->has_pending() && !page->commit_pending()) {
    update_apply();
    return;
  }
  if (store_.session_dirty() && page_marked(nav_[static_cast<std::size_t>(current_index_)])) {
    store_.drop_originals();
    dirty_ids_.clear();
  }
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
      shots_.push_back(shot);
    }
  }
  const int overview = std::max(0, nav_index("overview"));
  const int display = std::max(0, nav_index("display"));
  const int appearance = std::max(0, nav_index("appearance"));
  const int users = std::max(0, nav_index("users"));
  const int sizes[][2] = {{1100, 720}, {1000, 550}};
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
  compact.width = 1000;
  compact.height = 550;
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
    } else if (shot_index_ == 0) {
      resize(1200, 1040);
    }
    select_nav(shot.page);
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
