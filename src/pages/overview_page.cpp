// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/overview_page.hpp"

#include "logic/host_info.hpp"
#include "pages/appearance_page.hpp"
#include "pages/sound_page.hpp"
#include "pages/users_page.hpp"
#include "ui_util.hpp"

#include <cairomm/context.h>
#include <gdkmm/pixbuf.h>
#include <glibmm/markup.h>
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

Glib::RefPtr<Gdk::Pixbuf> load_themed_icon(const char* name, int size) {
  const char* themes[] = {"", "AdwaitaLegacy", "Humanity", "Humanity-Dark", "hicolor"};
  for (const char* theme_name : themes) {
    Glib::RefPtr<Gtk::IconTheme> theme;
    if (theme_name[0] == '\0') {
      theme = Gtk::IconTheme::get_default();
    } else {
      theme = Gtk::IconTheme::create();
      theme->set_custom_theme(theme_name);
    }
    if (!theme) {
      continue;
    }
    try {
      auto pixbuf = theme->load_icon(name, size, 1, Gtk::ICON_LOOKUP_FORCE_SIZE);
      if (pixbuf) {
        return pixbuf;
      }
    } catch (const Glib::Error&) {
    }
  }
  return {};
}

Glib::RefPtr<Gdk::Pixbuf> load_legacy_icon(const char* name, int size) {
  auto legacy = Gtk::IconTheme::create();
  legacy->set_custom_theme("AdwaitaLegacy");
  try {
    auto pixbuf = legacy->load_icon(name, size, 1, Gtk::ICON_LOOKUP_FORCE_SIZE);
    if (pixbuf) {
      return pixbuf;
    }
  } catch (const Glib::Error&) {
  }
  return load_themed_icon(name, size);
}

Glib::RefPtr<Gdk::Pixbuf> branding_logo(bool lcos, int size) {
  const char* lcos_names[] = {"lcos-logo", "start-here-lcos", "distributor-logo", "lcos"};
  const char* other_names[] = {"distributor-logo", "start-here"};
  const char** names = lcos ? lcos_names : other_names;
  const int count = lcos ? 4 : 2;
  for (int i = 0; i < count; ++i) {
    if (auto pixbuf = load_themed_icon(names[i], size)) {
      return pixbuf;
    }
  }
  return load_themed_icon("org.lunduke.LcosSettings", size);
}

void set_small_caps(Gtk::Label& label, const std::string& text) {
  label.set_markup("<span font_variant='smallcaps' weight='bold' letter_spacing='800'>" +
                   Glib::Markup::escape_text(text) + "</span>");
}

void paint_system_card(const Cairo::RefPtr<Cairo::Context>& context, double width, double height) {
  if (width < 2 || height < 2) {
    return;
  }
  constexpr double radius = 8;
  constexpr double pi = 3.141592653589793;
  constexpr double half_pi = pi / 2.0;
  auto rounded = [&](double x, double y, double w, double h) {
    context->begin_new_sub_path();
    context->arc(x + w - radius, y + radius, radius, -half_pi, 0);
    context->arc(x + w - radius, y + h - radius, radius, 0, half_pi);
    context->arc(x + radius, y + h - radius, radius, half_pi, pi);
    context->arc(x + radius, y + radius, radius, pi, 3 * half_pi);
    context->close_path();
  };
  context->save();
  context->set_source_rgba(0, 0, 0, 0.06);
  rounded(0, 1, width, height);
  context->fill();
  auto fill = Cairo::LinearGradient::create(0, 0, 0, height);
  fill->add_color_stop_rgb(0, 252.0 / 255.0, 252.0 / 255.0, 251.0 / 255.0);
  fill->add_color_stop_rgb(1, 244.0 / 255.0, 243.0 / 255.0, 241.0 / 255.0);
  context->set_source(fill);
  rounded(0, 0, width, height);
  context->fill();
  context->set_source_rgb(1, 1, 1);
  context->set_line_width(1);
  context->move_to(radius, 1);
  context->line_to(width - radius, 1);
  context->stroke();
  context->set_source_rgb(205.0 / 255.0, 201.0 / 255.0, 195.0 / 255.0);
  context->set_line_width(1);
  rounded(0.5, 0.5, width - 1, height - 1);
  context->stroke();
  context->restore();
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
  hero->signal_draw().connect(
      [hero](const Cairo::RefPtr<Cairo::Context>& context) {
        paint_system_card(context, hero->get_allocated_width(), hero->get_allocated_height());
        return false;
      },
      false);
  logo_ = Gtk::manage(new Gtk::Image());
  logo_->set_pixel_size(112);
  logo_->set_valign(Gtk::ALIGN_CENTER);
  hero->pack_start(*logo_, Gtk::PACK_SHRINK);

  auto* info = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
  info->set_valign(Gtk::ALIGN_CENTER);
  info->set_hexpand(true);
  kicker_ = Gtk::manage(new Gtk::Label());
  kicker_->set_halign(Gtk::ALIGN_START);
  kicker_->get_style_context()->add_class("lcos-kicker");
  version_ = Gtk::manage(new Gtk::Label());
  version_->set_halign(Gtk::ALIGN_START);
  version_->set_hexpand(true);
  version_->set_line_wrap(false);
  version_->set_ellipsize(Pango::ELLIPSIZE_END);
  version_->set_xalign(0);
  version_->get_style_context()->add_class("lcos-version");
  auto* host_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  host_row->pack_start(*icon_image("computer", 16), Gtk::PACK_SHRINK);
  hostname_ = Gtk::manage(new Gtk::Label());
  hostname_->set_halign(Gtk::ALIGN_START);
  hostname_->get_style_context()->add_class("lcos-host");
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
    cap->get_style_context()->add_class("lcos-fact");
    *values[i] = Gtk::manage(new Gtk::Label());
    (*values[i])->set_halign(Gtk::ALIGN_START);
    (*values[i])->set_hexpand(true);
    (*values[i])->set_line_wrap(false);
    (*values[i])->set_ellipsize(Pango::ELLIPSIZE_NONE);
    (*values[i])->set_xalign(0);
    (*values[i])->get_style_context()->add_class("lcos-fact");
    facts->attach(*cap, 0, i, 1, 1);
    facts->attach(**values[i], 1, i, 1, 1);
  }
  info->pack_start(*facts, Gtk::PACK_SHRINK);
  hero->pack_start(*info, Gtk::PACK_EXPAND_WIDGET);

  auto* quick = Gtk::manage(new Gtk::Grid());
  quick->set_column_spacing(10);
  quick->set_row_spacing(8);
  quick->set_valign(Gtk::ALIGN_CENTER);
  quick->set_hexpand(false);
  quick->get_style_context()->add_class("lcos-quick");
  auto* qh = Gtk::manage(new Gtk::Label());
  set_small_caps(*qh, "Quick settings");
  qh->set_halign(Gtk::ALIGN_START);
  qh->set_margin_bottom(2);
  qh->get_style_context()->add_class("lcos-kicker");
  quick->attach(*qh, 0, 0, 2, 1);
  auto* dark_label = make_label("Dark style");
  dark_label->set_halign(Gtk::ALIGN_START);
  dark_label->set_hexpand(true);
  dark_.set_halign(Gtk::ALIGN_END);
  dark_.set_valign(Gtk::ALIGN_CENTER);
  quick->attach(*dark_label, 0, 1, 1, 1);
  quick->attach(dark_, 1, 1, 1, 1);
  if (!appearance_.dark_style_available()) {
    dark_label->set_no_show_all(true);
    dark_.set_no_show_all(true);
    dark_label->hide();
    dark_.hide();
  }
  auto* login_label = make_label("Automatic login");
  login_label->set_halign(Gtk::ALIGN_START);
  login_label->set_hexpand(true);
  autologin_.set_halign(Gtk::ALIGN_END);
  autologin_.set_valign(Gtk::ALIGN_CENTER);
  quick->attach(*login_label, 0, 2, 1, 1);
  quick->attach(autologin_, 1, 2, 1, 1);
  auto* vol = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  vol->set_hexpand(true);
  vol->get_style_context()->add_class("lcos-volume");
  vol->pack_start(*icon_image("audio-volume-high", 16), Gtk::PACK_SHRINK);
  volume_.set_range(0, 100);
  volume_.set_draw_value(false);
  volume_.set_hexpand(true);
  volume_.set_valign(Gtk::ALIGN_CENTER);
  vol->pack_start(volume_, Gtk::PACK_EXPAND_WIDGET);
  quick->attach(*vol, 0, 3, 2, 1);
  hero->pack_start(*quick, Gtk::PACK_SHRINK);
  page->pack_start(*hero, Gtk::PACK_SHRINK);

  const char* sections[] = {"Look & Feel", "Hardware", "System"};
  for (const char* section : sections) {
    auto* tiles = Gtk::manage(new Gtk::Grid());
    tiles->set_hexpand(true);
    tiles->set_column_homogeneous(true);
    tiles->set_column_spacing(6);
    tiles->set_margin_top(12);
    int column = 0;
    for (const auto& tile : kTiles) {
      if (std::string(tile.section) != section) {
        continue;
      }
      auto* button = Gtk::manage(new Gtk::Button());
      button->set_relief(Gtk::RELIEF_NONE);
      button->set_hexpand(true);
      button->set_halign(Gtk::ALIGN_FILL);
      button->set_valign(Gtk::ALIGN_FILL);
      button->get_style_context()->add_class("lcos-tile");
      auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
      auto* image = icon_image(tile.icon, 32);
      image->set_halign(Gtk::ALIGN_CENTER);
      auto* label = Gtk::manage(new Gtk::Label(tile.title));
      label->set_halign(Gtk::ALIGN_CENTER);
      label->set_hexpand(true);
      label->set_justify(Gtk::JUSTIFY_CENTER);
      label->set_line_wrap(true);
      label->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
      label->set_lines(2);
      label->set_max_width_chars(12);
      label->set_ellipsize(Pango::ELLIPSIZE_END);
      label->get_style_context()->add_class("lcos-tile-label");
      box->pack_start(*image, Gtk::PACK_SHRINK);
      box->pack_start(*label, Gtk::PACK_SHRINK);
      button->add(*box);
      const std::string id = tile.id;
      button->signal_clicked().connect([this, id] { open(id); });
      tiles->attach(*button, column, 0, 1, 1);
      ++column;
    }
    if (column < 6) {
      auto* spacer = Gtk::manage(new Gtk::Box());
      spacer->set_hexpand(true);
      tiles->attach(*spacer, column, 0, 6 - column, 1);
    }
    auto* frame = make_frame(section, *tiles);
    frame->set_margin_top(14);
    page->pack_start(*frame, Gtk::PACK_SHRINK);
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
  if (logo_ != nullptr) {
    if (auto pixbuf = branding_logo(facts.lcos, 112)) {
      logo_->set(pixbuf);
    }
  }
  if (kicker_ != nullptr) {
    if (facts.lcos) {
      set_small_caps(*kicker_, "The Lunduke Computer Operating System");
      kicker_->show();
    } else if (!facts.kicker.empty()) {
      set_small_caps(*kicker_, facts.kicker);
      kicker_->show();
    } else {
      kicker_->hide();
    }
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
