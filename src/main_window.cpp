// SPDX-License-Identifier: GPL-3.0-or-later

#include "main_window.hpp"

#include "pages/appearance_page.hpp"
#include "pages/display_page.hpp"
#include "pages/keyboard_page.hpp"
#include "pages/mouse_page.hpp"
#include "pages/sound_page.hpp"

#include <gdk/gdkx.h>
#include <gdkmm/pixbuf.h>
#include <glibmm/main.h>
#include <gtkmm/box.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>

#include <iostream>

namespace lcos {
namespace {

struct Category {
  const char* id;
  const char* title;
  const char* icon;
};

const Category kCategories[] = {
    {"display", "Display", "video-display-symbolic"},
    {"appearance", "Appearance", "preferences-desktop-theme-symbolic"},
    {"keyboard", "Keyboard", "input-keyboard-symbolic"},
    {"mouse", "Mouse", "input-mouse-symbolic"},
    {"sound", "Sound", "audio-volume-high-symbolic"},
};

}  // namespace

MainWindow::MainWindow(XfconfStore& store)
    : store_(store),
      randr_(gdk_x11_display_get_xdisplay(gdk_display_get_default())),
      pointers_(gdk_x11_display_get_xdisplay(gdk_display_get_default())) {
  set_title("LCOS Command Center");
  set_default_size(1100, 760);
  set_icon_name("org.lunduke.LcosSettings");
  if (g_file_test("data/icons/hicolor/48x48/apps/org.lunduke.LcosSettings.png",
                  G_FILE_TEST_EXISTS)) {
    try {
      set_icon_from_file("data/icons/hicolor/48x48/apps/org.lunduke.LcosSettings.png");
    } catch (const Glib::Error&) {
    }
  }

  auto* display = Gtk::manage(new DisplayPage(store_, randr_, *this));
  auto* appearance = Gtk::manage(new AppearancePage(store_));
  auto* keyboard = Gtk::manage(new KeyboardPage(store_, *this));
  auto* mouse = Gtk::manage(new MousePage(store_, pointers_));
  auto* sound = Gtk::manage(new SoundPage(store_, pulse_));
  pages_ = {display, appearance, keyboard, mouse, sound};

  auto* root = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  add(*root);

  auto* body = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
  sidebar_.set_size_request(210, -1);
  sidebar_.get_style_context()->add_class("lcos-sidebar");
  sidebar_.set_selection_mode(Gtk::SELECTION_BROWSE);
  for (int i = 0; i < 5; ++i) {
    auto* row = Gtk::manage(new Gtk::ListBoxRow());
    auto* line = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
    line->set_margin_top(8);
    line->set_margin_bottom(8);
    line->set_margin_start(12);
    line->set_margin_end(12);
    auto* image = Gtk::manage(new Gtk::Image());
    image->set_from_icon_name(kCategories[i].icon, Gtk::ICON_SIZE_LARGE_TOOLBAR);
    auto* label = Gtk::manage(new Gtk::Label(kCategories[i].title));
    label->set_halign(Gtk::ALIGN_START);
    line->pack_start(*image, Gtk::PACK_SHRINK);
    line->pack_start(*label, Gtk::PACK_EXPAND_WIDGET);
    row->add(*line);
    sidebar_.append(*row);
    stack_.add(*pages_[static_cast<std::size_t>(i)], kCategories[i].id);
  }
  sidebar_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
    if (row == nullptr) {
      return;
    }
    select_page(row->get_index());
  });
  body->pack_start(sidebar_, Gtk::PACK_SHRINK);
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
  } else {
    status_->set_text(
        "OK keeps this visit. Cancel, and closing the window, restores it.");
  }
  auto* ok = Gtk::manage(new Gtk::Button("OK"));
  apply_ = Gtk::manage(new Gtk::Button("Apply"));
  auto* cancel = Gtk::manage(new Gtk::Button("Cancel"));
  ok->set_can_default(true);
  buttons->pack_start(*status_, Gtk::PACK_EXPAND_WIDGET);
  buttons->pack_start(*ok, Gtk::PACK_SHRINK);
  buttons->pack_start(*apply_, Gtk::PACK_SHRINK);
  buttons->pack_start(*cancel, Gtk::PACK_SHRINK);
  root->pack_start(*buttons, Gtk::PACK_SHRINK);

  ok->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::close_keep));
  apply_->signal_clicked().connect([this] {
    if (pages_.empty()) {
      return;
    }
    pages_.front()->commit_pending();
    update_apply();
  });
  cancel->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::close_revert));
  for (auto* page : pages_) {
    page->pending_changed.connect(sigc::mem_fun(*this, &MainWindow::update_apply));
  }

  sidebar_.select_row(*sidebar_.get_row_at_index(0));
  select_page(0);
  update_apply();
  show_all();
}

void MainWindow::select_page(int index) {
  if (index < 0 || index >= static_cast<int>(pages_.size())) {
    return;
  }
  stack_.set_visible_child(*pages_[static_cast<std::size_t>(index)]);
  heading_->set_text(pages_[static_cast<std::size_t>(index)]->title());
  summary_->set_text(pages_[static_cast<std::size_t>(index)]->summary());
  pages_[static_cast<std::size_t>(index)]->on_page_shown();
  update_apply();
}

void MainWindow::update_apply() {
  if (apply_ == nullptr || pages_.empty()) {
    return;
  }
  apply_->set_sensitive(pages_.front()->has_pending());
}

void MainWindow::close_keep() {
  if (closing_) {
    return;
  }
  if (!pages_.empty() && !pages_.front()->commit_pending()) {
    update_apply();
    return;
  }
  closing_ = true;
  hide();
}

void MainWindow::close_revert() {
  if (closing_) {
    return;
  }
  closing_ = true;
  auto* display = dynamic_cast<DisplayPage*>(pages_.empty() ? nullptr : pages_.front());
  store_.revert_session();
  if (display != nullptr) {
    display->restore_session_hardware();
  }
  if (store_.channel_touched("displays")) {
    store_.set_string_untracked("displays", "/Schemes/Apply", "Default");
  }
  hide();
}

bool MainWindow::on_delete_event(GdkEventAny*) {
  close_revert();
  return false;
}

void MainWindow::dump_pages(const std::string& directory) {
  dump_dir_ = directory;
  dump_index_ = 0;
  dump_phase_ = 0;
  resize(1100, 980);
  Glib::signal_timeout().connect(sigc::mem_fun(*this, &MainWindow::dump_step), 400);
}

bool MainWindow::dump_step() {
  if (dump_index_ >= static_cast<int>(pages_.size())) {
    hide();
    return false;
  }
  if (dump_phase_ == 0) {
    if (auto* row = sidebar_.get_row_at_index(dump_index_)) {
      sidebar_.select_row(*row);
    }
    select_page(dump_index_);
    dump_phase_ = 1;
    return true;
  }
  auto window = get_window();
  if (window) {
    const int width = std::max(1, get_allocated_width());
    const int height = std::max(1, get_allocated_height());
    try {
      auto pixbuf = Gdk::Pixbuf::create(window, 0, 0, width, height);
      const std::string path =
          dump_dir_ + "/" + std::string(kCategories[dump_index_].id) + ".png";
      pixbuf->save(path, "png");
      std::cerr << "wrote " << path << "\n";
    } catch (const Glib::Error& error) {
      std::cerr << "screenshot failed: " << error.what() << "\n";
    }
  }
  dump_phase_ = 0;
  ++dump_index_;
  if (dump_index_ >= static_cast<int>(pages_.size())) {
    hide();
    return false;
  }
  return true;
}

}  // namespace lcos
