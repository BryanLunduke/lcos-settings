// SPDX-License-Identifier: GPL-3.0-or-later

#include "main_window.hpp"

#include "pages/accessibility_page.hpp"
#include "pages/appearance_page.hpp"
#include "pages/datetime_page.hpp"
#include "pages/desktop_page.hpp"
#include "pages/display_page.hpp"
#include "pages/language_page.hpp"
#include "pages/keyboard_page.hpp"
#include "pages/media_page.hpp"
#include "pages/mouse_page.hpp"
#include "pages/notifications_page.hpp"
#include "pages/power_page.hpp"
#include "pages/software_page.hpp"
#include "pages/sound_page.hpp"
#include "pages/startup_page.hpp"
#include "pages/users_page.hpp"
#include "pages/window_page.hpp"
#include "pages/window_tweaks_page.hpp"
#include "ui_util.hpp"

#include <gdk/gdkx.h>
#include <gdkmm/general.h>
#include <gdkmm/pixbuf.h>
#include <glibmm/main.h>
#include <gtkmm/box.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>

#include <algorithm>
#include <iostream>

namespace lcos {

MainWindow::MainWindow(XfconfStore& store)
    : store_(store),
      randr_(gdk_x11_display_get_xdisplay(gdk_display_get_default())),
      pointers_(gdk_x11_display_get_xdisplay(gdk_display_get_default())) {
  set_title("LCOS Command Center");
  set_default_size(1100, 760);
  Gdk::Geometry hints;
  hints.min_width = 760;
  hints.min_height = 480;
  set_geometry_hints(*this, hints, Gdk::HINT_MIN_SIZE);
  set_icon_name("org.lunduke.LcosSettings");
  if (g_file_test("data/icons/hicolor/48x48/apps/org.lunduke.LcosSettings.png",
                  G_FILE_TEST_EXISTS)) {
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
  pages_ = {display, desktop, appearance, windows, tweaks, keyboard, mouse, accessibility,
            power, notifications, media, sound, datetime, language, users, startup, software};
  nav_ = {
      {"display", "Display", "preferences-desktop-display"},
      {"desktop", "Desktop", "preferences-desktop-wallpaper"},
      {"appearance", "Appearance", "preferences-desktop-theme"},
      {"window-manager", "Window Manager", "preferences-system-windows"},
      {"window-tweaks", "Window Manager Tweaks", "preferences-system"},
      {"keyboard", "Keyboard", "preferences-desktop-keyboard"},
      {"mouse", "Mouse", "preferences-desktop-peripherals"},
      {"accessibility", "Accessibility", "preferences-desktop-accessibility"},
      {"power", "Power", "battery"},
      {"notifications", "Notifications", "preferences-system-notifications"},
      {"removable-media", "Removable Drives and Media", "drive-removable-media"},
      {"sound", "Sound", "audio-volume-high"},
      {"date-time", "Date & Time", "appointment-soon"},
      {"language", "Language", "preferences-desktop-locale"},
      {"users", "Users", "system-users"},
      {"startup", "Startup", "system-run"},
      {"default-software", "Default Software", "applications-other"},
  };

  auto* root = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  add(*root);

  auto* body = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
  // The category list is taller than a 1024x768 screen. Scrolling it lets the
  // window shrink to about 1000x550 so OK, Apply, and Cancel stay on screen.
  auto* side_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*side_scroll);
  side_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  side_scroll->set_size_request(248, -1);
  side_scroll->set_min_content_width(248);
  side_scroll->set_min_content_height(160);
  sidebar_.set_size_request(248, -1);
  sidebar_.get_style_context()->add_class("lcos-sidebar");
  sidebar_.set_selection_mode(Gtk::SELECTION_BROWSE);
  for (std::size_t i = 0; i < pages_.size(); ++i) {
    auto* row = Gtk::manage(new Gtk::ListBoxRow());
    auto* line = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
    line->set_margin_top(8);
    line->set_margin_bottom(8);
    line->set_margin_start(12);
    line->set_margin_end(12);
    auto* image = Gtk::manage(new Gtk::Image());
    image->set_halign(Gtk::ALIGN_CENTER);
    image->set_valign(Gtk::ALIGN_CENTER);
    image->set_size_request(24, 24);
    nav_images_.push_back(image);
    auto* label = Gtk::manage(new Gtk::Label(nav_[i].title));
    label->set_halign(Gtk::ALIGN_START);
    label->set_valign(Gtk::ALIGN_CENTER);
    label->set_line_wrap(true);
    label->set_max_width_chars(22);
    label->set_xalign(0);
    line->set_valign(Gtk::ALIGN_CENTER);
    line->pack_start(*image, Gtk::PACK_SHRINK);
    line->pack_start(*label, Gtk::PACK_EXPAND_WIDGET);
    row->add(*line);
    sidebar_.append(*row);
    stack_.add(*pages_[i], nav_[i].id);
  }
  sidebar_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
    if (row == nullptr) {
      return;
    }
    select_page(row->get_index());
  });
  side_scroll->add(sidebar_);
  body->pack_start(*side_scroll, Gtk::PACK_SHRINK);
  stack_.set_homogeneous(false);
  stack_.set_vexpand(true);
  auto* content = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));
  content->set_border_width(8);
  heading_ = Gtk::manage(new Gtk::Label());
  heading_->set_halign(Gtk::ALIGN_START);
  heading_->get_style_context()->add_class("lcos-title");
  summary_ = Gtk::manage(new Gtk::Label());
  summary_->set_halign(Gtk::ALIGN_START);
  summary_->set_line_wrap(true);
  summary_->get_style_context()->add_class("lcos-subtitle");
  content->pack_start(*heading_, Gtk::PACK_SHRINK);
  content->pack_start(*summary_, Gtk::PACK_SHRINK);
  content->pack_start(stack_, Gtk::PACK_EXPAND_WIDGET);
  body->pack_start(*content, Gtk::PACK_EXPAND_WIDGET);
  root->pack_start(*body, Gtk::PACK_EXPAND_WIDGET);
  root->pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL)),
                   Gtk::PACK_SHRINK);

  auto* buttons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  buttons->set_margin_top(8);
  buttons->set_margin_bottom(8);
  buttons->set_margin_start(12);
  buttons->set_margin_end(12);
  status_ = Gtk::manage(new Gtk::Label());
  status_->set_halign(Gtk::ALIGN_START);
  status_->set_line_wrap(true);
  status_->set_hexpand(true);
  if (!store_.available()) {
    status_->set_text("Settings service is not available. Changes cannot be saved.");
  }
  auto* ok = Gtk::manage(new Gtk::Button("_OK", true));
  apply_ = Gtk::manage(new Gtk::Button("_Apply", true));
  auto* cancel = Gtk::manage(new Gtk::Button("_Cancel", true));
  ok->set_can_default(true);
  buttons->pack_start(*status_, Gtk::PACK_EXPAND_WIDGET);
  buttons->pack_start(*ok, Gtk::PACK_SHRINK);
  buttons->pack_start(*apply_, Gtk::PACK_SHRINK);
  buttons->pack_start(*cancel, Gtk::PACK_SHRINK);
  root->pack_start(*buttons, Gtk::PACK_SHRINK);

  ok->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::close_keep));
  apply_->signal_clicked().connect([this] {
    for (auto* page : pages_) {
      if (page->has_pending() && !page->commit_pending()) {
        update_apply();
        return;
      }
    }
    if (privileged_.busy()) {
      privileged_.wait_until_idle();
    }
    const PrivResult kept = privileged_.checkpoint();
    if (!kept.ok) {
      tell(kept.error.empty() ? "The privileged changes could not be kept." : kept.error);
      update_apply();
      return;
    }
    // Apply is a checkpoint. Cancel after this restores only newer edits.
    store_.drop_originals();
    update_apply();
  });
  cancel->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::close_revert));
  for (auto* page : pages_) {
    page->pending_changed.connect(sigc::mem_fun(*this, &MainWindow::update_apply));
  }
  store_.changed.connect(sigc::mem_fun(*this, &MainWindow::update_apply));
  privileged_.changed.connect(sigc::mem_fun(*this, &MainWindow::update_apply));

  sidebar_.select_row(*sidebar_.get_row_at_index(0));
  select_page(0);
  update_apply();
  show_all();
  load_nav_icons();
  signal_realize().connect(sigc::mem_fun(*this, &MainWindow::load_nav_icons));
  property_scale_factor().signal_changed().connect(
      sigc::mem_fun(*this, &MainWindow::load_nav_icons));
  // The first expose can run before the stack has allocated the opening page.
  Glib::signal_idle().connect([this]() {
    queue_draw();
    return false;
  });
}

void MainWindow::load_nav_icons() {
  const int scale = std::max(1, get_scale_factor());
  auto legacy = Gtk::IconTheme::create();
  legacy->set_custom_theme("AdwaitaLegacy");
  auto fallback = Gtk::IconTheme::get_default();
  auto load = [&](const Glib::RefPtr<Gtk::IconTheme>& theme, const Glib::ustring& name) {
    if (!theme) {
      return Glib::RefPtr<Gdk::Pixbuf>();
    }
    try {
      return theme->load_icon(name, 24, scale, Gtk::ICON_LOOKUP_FORCE_SIZE);
    } catch (const Glib::Error&) {
      return Glib::RefPtr<Gdk::Pixbuf>();
    }
  };
  for (std::size_t i = 0; i < nav_images_.size() && i < nav_.size(); ++i) {
    auto pixbuf = load(legacy, nav_[i].icon);
    if (!pixbuf) {
      pixbuf = load(fallback, nav_[i].icon);
    }
    if (!pixbuf) {
      continue;
    }
    nav_images_[i]->set(Gdk::Cairo::create_surface_from_pixbuf(pixbuf, scale));
  }
}

void MainWindow::show_page(const std::string& id) {
  for (std::size_t i = 0; i < nav_.size(); ++i) {
    if (nav_[i].id == id) {
      if (auto* row = sidebar_.get_row_at_index(static_cast<int>(i))) {
        sidebar_.select_row(*row);
      }
      select_page(static_cast<int>(i));
      return;
    }
  }
}

void MainWindow::select_page(int index) {
  if (index < 0 || index >= static_cast<int>(pages_.size())) {
    return;
  }
  stack_.set_visible_child(*pages_[static_cast<std::size_t>(index)]);
  pages_[static_cast<std::size_t>(index)]->queue_draw();
  heading_->set_text(pages_[static_cast<std::size_t>(index)]->title());
  summary_->set_text(pages_[static_cast<std::size_t>(index)]->summary());
  pages_[static_cast<std::size_t>(index)]->on_page_shown();
  update_apply();
}

void MainWindow::update_apply() {
  if (apply_ == nullptr) {
    return;
  }
  bool pending = privileged_.dirty() || store_.session_dirty();
  for (auto* page : pages_) {
    pending = pending || page->has_pending();
  }
  apply_->set_sensitive(pending);
}

void MainWindow::tell(const std::string& message) {
  Gtk::MessageDialog dialog(*this, message, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
  dialog.run();
}

void MainWindow::close_keep() {
  if (closing_) {
    return;
  }
  for (auto* page : pages_) {
    if (page->has_pending() && !page->commit_pending()) {
      update_apply();
      return;
    }
  }
  if (privileged_.busy()) {
    privileged_.wait_until_idle();
  }
  const PrivResult committed = privileged_.commit();
  if (!committed.ok) {
    tell(committed.error.empty() ? "The privileged changes could not be kept." : committed.error);
    return;
  }
  store_.drop_originals();
  closing_ = true;
  hide();
}

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
  closing_ = true;
  const bool displays = store_.channel_touched("displays");
  DisplayPage* display = nullptr;
  for (auto* page : pages_) {
    if (display == nullptr) {
      display = dynamic_cast<DisplayPage*>(page);
    }
    page->revert_page();
  }
  const bool uncommitted_display = display != nullptr && display->uncommitted_hardware();
  store_.revert_session();
  // A missing Default scheme cannot be applied after the RandR restore: that
  // is what left a rejected mode on screen when the window closed.
  if (displays && !uncommitted_display) {
    store_.set_string_untracked("displays", "/Schemes/Apply", "Default");
  }
  if (display != nullptr) {
    display->restore_session_hardware();
  }
  hide();
}

bool MainWindow::on_delete_event(GdkEventAny*) {
  close_revert();
  // The window deletes itself from the hide handler. Returning false lets
  // GTK destroy it as well, which frees the same gtkmm object twice.
  return true;
}

void MainWindow::dump_pages(const std::string& directory) {
  dump_dir_ = directory;
  shots_.clear();
  for (int page = 0; page < static_cast<int>(pages_.size()); ++page) {
    const int frames = std::max(1, pages_[static_cast<std::size_t>(page)]->screenshot_count());
    for (int frame = 0; frame < frames; ++frame) {
      Shot shot;
      shot.page = page;
      shot.frame = frame;
      shot.name = nav_[static_cast<std::size_t>(page)].id;
      const std::string suffix =
          pages_[static_cast<std::size_t>(page)]->screenshot_suffix(frame);
      if (!suffix.empty()) {
        shot.name += "-" + suffix;
      }
      shots_.push_back(shot);
    }
  }
  Shot compact;
  compact.page = static_cast<int>(pages_.size()) - 1;
  compact.frame = 1;
  compact.name = "window-short";
  compact.width = 1000;
  compact.height = 550;
  shots_.push_back(compact);
  shot_index_ = 0;
  dump_phase_ = 0;
  resize(1200, 1040);
  Glib::signal_timeout().connect(sigc::mem_fun(*this, &MainWindow::dump_step), 500);
}

bool MainWindow::dump_step() {
  if (shot_index_ >= static_cast<int>(shots_.size())) {
    hide();
    return false;
  }
  const Shot& shot = shots_[static_cast<std::size_t>(shot_index_)];
  if (dump_phase_ == 0) {
    if (auto* row = sidebar_.get_row_at_index(shot.page)) {
      sidebar_.select_row(*row);
    }
    if (shot.width > 0 && shot.height > 0) {
      resize(shot.width, shot.height);
    } else if (shot_index_ == 0) {
      resize(1200, 1040);
    }
    select_page(shot.page);
    pages_[static_cast<std::size_t>(shot.page)]->prepare_screenshot(shot.frame);
    pages_[static_cast<std::size_t>(shot.page)]->queue_draw();
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
  dump_phase_ = 0;
  ++shot_index_;
  if (shot_index_ >= static_cast<int>(shots_.size())) {
    hide();
    return false;
  }
  return true;
}

}  // namespace lcos
