// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/overview_page.hpp"

#include "logic/host_info.hpp"
#include "pages/appearance_page.hpp"
#include "pages/sound_page.hpp"
#include "pages/users_page.hpp"
#include "ui_util.hpp"

#include <gdkmm/pixbuf.h>
#include <gtkmm/button.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/grid.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>

#include <vector>

namespace lcos {
namespace {

struct TileSpec {
  const char* id;
  const char* title;
  const char* icon;
  const char* section;
};

const TileSpec kTiles[] = {
    {"appearance", "Appearance", "preferences-desktop-theme", "Look & Feel"},
    {"desktop", "Desktop", "preferences-desktop-wallpaper", "Look & Feel"},
    {"window-manager", "Window Manager", "preferences-system-windows", "Look & Feel"},
    {"window-tweaks", "WM Tweaks", "preferences-system", "Look & Feel"},
    {"notifications", "Notifications", "preferences-system-notifications", "Look & Feel"},
    {"display", "Display", "preferences-desktop-display", "Hardware"},
    {"sound", "Sound", "audio-volume-high", "Hardware"},
    {"power", "Power", "battery", "Hardware"},
    {"keyboard", "Keyboard", "preferences-desktop-keyboard", "Hardware"},
    {"mouse", "Mouse", "input-mouse", "Hardware"},
    {"removable-media", "Removable Media", "drive-removable-media", "Hardware"},
    {"date-time", "Date & Time", "appointment-soon", "System"},
    {"language", "Language", "preferences-desktop-locale", "System"},
    {"users", "Users", "system-users", "System"},
    {"startup", "Startup", "system-run", "System"},
    {"default-software", "Default Software", "preferences-other", "System"},
    {"accessibility", "Accessibility", "preferences-desktop-accessibility", "System"},
};

Glib::RefPtr<Gdk::Pixbuf> load_legacy_icon(const char* name, int size) {
  auto legacy = Gtk::IconTheme::create();
  legacy->set_custom_theme("AdwaitaLegacy");
  try {
    return legacy->load_icon(name, size, 1, Gtk::ICON_LOOKUP_FORCE_SIZE);
  } catch (const Glib::Error&) {
  }
  try {
    return Gtk::IconTheme::get_default()->load_icon(name, size, 1, Gtk::ICON_LOOKUP_FORCE_SIZE);
  } catch (const Glib::Error&) {
  }
  return {};
}

Gtk::Image* icon_image(const char* name, int size) {
  auto* image = Gtk::manage(new Gtk::Image());
  image->set_pixel_size(size);
  if (auto pixbuf = load_legacy_icon(name, size)) {
    image->set(pixbuf);
  }
  return image;
}

}  // namespace

OverviewPage::OverviewPage(AppearancePage& appearance, SoundPage& sound, UsersPage& users,
                           PulseClient& pulse, const std::function<void(const std::string&)>& open_page,
                           const std::function<void(const std::string&)>& mark_page)
    : appearance_(appearance),
      sound_(sound),
      users_(users),
      pulse_(pulse),
      open_page_(open_page),
      mark_page_(mark_page),
      volume_(Gtk::ORIENTATION_HORIZONTAL) {
  auto* page = make_vbox(0);
  page->set_border_width(0);

  auto* hero = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 24));
  hero->get_style_context()->add_class("lcos-card");
  hero->set_margin_bottom(6);
  auto* logo = icon_image("org.lunduke.LcosSettings", 112);
  if (logo->get_storage_type() == Gtk::IMAGE_EMPTY) {
    const char* paths[] = {"data/icons/hicolor/128x128/apps/org.lunduke.LcosSettings.png",
                           "/usr/share/icons/hicolor/128x128/apps/org.lunduke.LcosSettings.png"};
    for (const char* path : paths) {
      if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        continue;
      }
      try {
        logo->set(Gdk::Pixbuf::create_from_file(path, 112, 112, true));
        break;
      } catch (const Glib::Error&) {
      }
    }
  }
  logo->set_valign(Gtk::ALIGN_CENTER);
  logo->set_margin_start(6);
  hero->pack_start(*logo, Gtk::PACK_SHRINK);

  auto* info = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
  info->set_valign(Gtk::ALIGN_CENTER);
  info->set_hexpand(true);
  kicker_ = Gtk::manage(new Gtk::Label());
  kicker_->set_halign(Gtk::ALIGN_START);
  kicker_->get_style_context()->add_class("lcos-kicker");
  version_ = Gtk::manage(new Gtk::Label());
  version_->set_halign(Gtk::ALIGN_START);
  version_->set_hexpand(true);
  version_->set_ellipsize(Pango::ELLIPSIZE_END);
  version_->get_style_context()->add_class("lcos-version");
  auto* host_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  host_row->pack_start(*icon_image("computer", 16), Gtk::PACK_SHRINK);
  hostname_ = Gtk::manage(new Gtk::Label());
  hostname_->set_halign(Gtk::ALIGN_START);
  host_row->pack_start(*hostname_, Gtk::PACK_SHRINK);
  info->pack_start(*kicker_, Gtk::PACK_SHRINK);
  info->pack_start(*version_, Gtk::PACK_SHRINK);
  info->pack_start(*host_row, Gtk::PACK_SHRINK);

  auto* facts = Gtk::manage(new Gtk::Grid());
  facts->set_column_spacing(12);
  facts->set_row_spacing(3);
  facts->set_margin_top(10);
  const char* captions[] = {"Processor", "Memory", "Graphics", "Storage"};
  Gtk::Label** values[] = {&processor_, &memory_, &graphics_, &storage_};
  for (int i = 0; i < 4; ++i) {
    auto* cap = Gtk::manage(new Gtk::Label(captions[i]));
    cap->set_halign(Gtk::ALIGN_START);
    cap->get_style_context()->add_class("lcos-faint");
    *values[i] = Gtk::manage(new Gtk::Label());
    (*values[i])->set_halign(Gtk::ALIGN_START);
    (*values[i])->set_hexpand(true);
    (*values[i])->set_ellipsize(Pango::ELLIPSIZE_END);
    facts->attach(*cap, 0, i, 1, 1);
    facts->attach(**values[i], 1, i, 1, 1);
  }
  info->pack_start(*facts, Gtk::PACK_SHRINK);
  hero->pack_start(*info, Gtk::PACK_EXPAND_WIDGET);

  auto* quick = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 12));
  quick->set_margin_start(18);
  quick->set_valign(Gtk::ALIGN_CENTER);
  quick->get_style_context()->add_class("lcos-quick");
  auto* qh = Gtk::manage(new Gtk::Label("Quick settings"));
  qh->set_halign(Gtk::ALIGN_START);
  qh->get_style_context()->add_class("lcos-kicker");
  quick->pack_start(*qh, Gtk::PACK_SHRINK);
  dark_row_ = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
  auto* dark_label = make_label("Dark style");
  dark_label->set_hexpand(true);
  dark_row_->pack_start(*dark_label, Gtk::PACK_EXPAND_WIDGET);
  dark_row_->pack_start(dark_, Gtk::PACK_SHRINK);
  if (appearance_.dark_style_available()) {
    quick->pack_start(*dark_row_, Gtk::PACK_SHRINK);
  } else {
    dark_row_->set_no_show_all(true);
    dark_row_->hide();
  }
  auto* login_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
  auto* login_label = make_label("Automatic login");
  login_label->set_hexpand(true);
  login_row->pack_start(*login_label, Gtk::PACK_EXPAND_WIDGET);
  login_row->pack_start(autologin_, Gtk::PACK_SHRINK);
  quick->pack_start(*login_row, Gtk::PACK_SHRINK);
  auto* vol = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  vol->pack_start(*icon_image("audio-volume-high", 16), Gtk::PACK_SHRINK);
  volume_.set_range(0, 100);
  volume_.set_draw_value(false);
  volume_.set_hexpand(true);
  vol->pack_start(volume_, Gtk::PACK_EXPAND_WIDGET);
  quick->pack_start(*vol, Gtk::PACK_SHRINK);
  hero->pack_start(*quick, Gtk::PACK_SHRINK);
  page->pack_start(*hero, Gtk::PACK_SHRINK);

  const char* sections[] = {"Look & Feel", "Hardware", "System"};
  for (const char* section : sections) {
    auto* grid = Gtk::manage(new Gtk::Grid());
    grid->set_column_spacing(6);
    grid->set_row_spacing(6);
    grid->set_margin_start(12);
    grid->set_margin_top(12);
    int column = 0;
    int row = 0;
    for (const auto& tile : kTiles) {
      if (std::string(tile.section) != section) {
        continue;
      }
      auto* button = Gtk::manage(new Gtk::Button());
      button->set_relief(Gtk::RELIEF_NONE);
      button->get_style_context()->add_class("lcos-tile");
      auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
      box->set_margin_top(8);
      box->set_margin_bottom(8);
      auto* image = icon_image(tile.icon, 32);
      image->set_halign(Gtk::ALIGN_CENTER);
      auto* label = Gtk::manage(new Gtk::Label(tile.title));
      label->set_justify(Gtk::JUSTIFY_CENTER);
      label->set_line_wrap(true);
      label->set_max_width_chars(12);
      label->get_style_context()->add_class("lcos-tile-label");
      box->pack_start(*image, Gtk::PACK_SHRINK);
      box->pack_start(*label, Gtk::PACK_SHRINK);
      button->add(*box);
      const std::string id = tile.id;
      button->signal_clicked().connect([this, id] { open(id); });
      grid->attach(*button, column, row, 1, 1);
      ++column;
      if (column == 6) {
        column = 0;
        ++row;
      }
    }
    page->pack_start(*make_frame(section, *grid), Gtk::PACK_SHRINK);
  }
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  dark_.set_size_request(48, 22);
  autologin_.set_size_request(48, 22);
  dark_.property_active().signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    appearance_.set_dark_style(dark_.get_active());
    if (mark_page_) {
      mark_page_("appearance");
    }
  });
  autologin_.property_active().signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    users_.set_automatic_for_current(autologin_.get_active());
    if (mark_page_) {
      mark_page_("users");
    }
  });
  volume_.signal_button_press_event().connect([this](GdkEventButton*) {
    volume_dragging_ = true;
    return false;
  });
  volume_.signal_button_release_event().connect([this](GdkEventButton*) {
    volume_dragging_ = false;
    return false;
  });
  volume_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    sound_.set_output_volume_percent(static_cast<int>(volume_.get_value()));
  });
  pulse_connection_ = pulse_.changed.connect([this] { refresh_quick(); });
  refresh_host();
  refresh_quick();
  show_all();
}

void OverviewPage::on_page_shown() {
  refresh_host();
  refresh_quick();
}

void OverviewPage::open(const std::string& id) {
  if (open_page_) {
    open_page_(id);
  }
}

void OverviewPage::refresh_host() {
  const HostFacts facts = read_host_facts();
  if (kicker_ != nullptr) {
    kicker_->set_text(facts.kicker);
    kicker_->set_visible(!facts.kicker.empty());
  }
  if (version_ != nullptr) {
    version_->set_text(facts.version_line);
  }
  if (hostname_ != nullptr) {
    hostname_->set_text(facts.hostname);
  }
  if (processor_ != nullptr) {
    processor_->set_text(facts.processor);
  }
  if (memory_ != nullptr) {
    memory_->set_text(facts.memory);
  }
  if (graphics_ != nullptr) {
    graphics_->set_text(facts.graphics);
  }
  if (storage_ != nullptr) {
    storage_->set_text(facts.storage);
  }
}

void OverviewPage::refresh_quick() {
  Freeze freeze(freeze_);
  if (appearance_.dark_style_available()) {
    if (dark_row_ != nullptr) {
      dark_row_->set_no_show_all(false);
      dark_row_->show();
    }
    dark_.set_active(appearance_.dark_style_on());
  }
  autologin_.set_active(users_.automatic_for_current());
  if (!volume_dragging_ && sound_.output_ready()) {
    const int percent = sound_.output_volume_percent();
    if (percent > 100) {
      volume_.set_range(0, 150);
    }
    volume_.set_value(percent);
  }
  volume_.set_sensitive(sound_.output_ready());
}

}  // namespace lcos
