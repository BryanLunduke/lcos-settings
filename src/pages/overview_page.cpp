// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/overview_page.hpp"

#include "logic/helper_status.hpp"

#include "logic/host_info.hpp"
#include "logic/system_model.hpp"
#include "pages/appearance_page.hpp"
#include "pages/sound_page.hpp"
#include "pages/users_page.hpp"
#include "ui_util.hpp"

#include <cairomm/context.h>
#include <gdkmm/general.h>
#include <gdkmm/pixbuf.h>
#include <glibmm/markup.h>
#include <gtkmm/button.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/grid.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>

#include <algorithm>
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
    {"appearance", "Appearance", "preferences-desktop-theme", "Look and Feel"},
    {"desktop", "Desktop", "preferences-desktop-wallpaper", "Look and Feel"},
    {"window-manager", "Window Manager", "preferences-system-windows", "Look and Feel"},
    {"window-tweaks", "Window Manager Tweaks", "preferences-system", "Look and Feel"},
    {"notifications", "Notifications", "preferences-system-notifications", "Look and Feel"},
    {"display", "Display", "preferences-desktop-display", "Hardware"},
    {"sound", "Sound", "audio-volume-high", "Hardware"},
    {"power", "Power", "battery", "Hardware"},
    {"keyboard", "Keyboard", "preferences-desktop-keyboard", "Hardware"},
    {"mouse", "Mouse", "input-mouse", "Hardware"},
    {"removable-media", "Removable Drives and Media", "drive-removable-media", "Hardware"},
    {"date-time", "Date and Time", "appointment-soon", "System"},
    {"language", "Language", "preferences-desktop-locale", "System"},
    {"users", "Users", "system-users", "System"},
    {"startup", "Startup", "system-run", "System"},
    {"default-software", "Default Software", "preferences-other", "System"},
    {"accessibility", "Accessibility", "preferences-desktop-accessibility", "System"},
};

constexpr int kLogoSize = 112;

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

// Load the LCOS badge at the exact device-pixel size (logical size x window
// scale factor) so GTK never upscales a smaller asset. The installed
// lcos-branding package provides lcos-logo in hicolor 16..512 (+@2x); the
// /usr/share/pixmaps fallback is only used on older systems.
Glib::RefPtr<Gdk::Pixbuf> branding_logo(bool lcos, int size, int scale) {
  const char* lcos_names[] = {"lcos-logo", "start-here-lcos", "distributor-logo", "lcos"};
  const char* other_names[] = {"distributor-logo", "start-here"};
  const char** names = lcos ? lcos_names : other_names;
  const int count = lcos ? 4 : 2;
  auto theme = Gtk::IconTheme::get_default();
  for (int i = 0; theme && i < count; ++i) {
    try {
      // Ask for the next installed size at or above the target (128 for the
      // 112 px card) so GTK only ever downscales, then filter it down with
      // HYPER ourselves instead of GTK's bilinear FORCE_SIZE path.
      const int fetch = size <= 128 ? 128 : 256;
      auto pixbuf = theme->load_icon(names[i], fetch, std::max(1, scale), Gtk::IconLookupFlags(0));
      if (pixbuf) {
        const int want = size * std::max(1, scale);
        if (pixbuf->get_width() != want || pixbuf->get_height() != want) {
          pixbuf = pixbuf->scale_simple(want, want, Gdk::INTERP_HYPER);
        }
        return pixbuf;
      }
    } catch (const Glib::Error&) {
    }
  }
  if (auto pixbuf = load_themed_icon(names[0], size * std::max(1, scale))) {
    return pixbuf;
  }
  return load_themed_icon("org.lunduke.LcosSettings", size * std::max(1, scale));
}

void set_small_caps(Gtk::Label& label, const std::string& text) {
  label.set_markup("<span font_variant='smallcaps' weight='bold' letter_spacing='800'>" +
                   Glib::Markup::escape_text(text) + "</span>");
}

void paint_system_card(const Cairo::RefPtr<Cairo::Context>& context, Gtk::Widget& widget, double width,
                       double height) {
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
  Gdk::RGBA base;
  Gdk::RGBA bg;
  base.set_rgba(0.97, 0.965, 0.96, 1);
  bg.set_rgba(0.93, 0.925, 0.91, 1);
  auto style = widget.get_style_context();
  Gdk::RGBA theme_base;
  Gdk::RGBA theme_bg;
  Gdk::RGBA theme_fg;
  if (style->lookup_color("theme_base_color", theme_base)) {
    base = theme_base;
  }
  if (style->lookup_color("theme_bg_color", theme_bg)) {
    bg = theme_bg;
  }
  theme_fg.set_rgba(0.2, 0.2, 0.2, 0.28);
  style->lookup_color("theme_fg_color", theme_fg);
  context->save();
  context->set_source_rgba(0, 0, 0, 0.06);
  rounded(0, 1, width, height);
  context->fill();
  auto fill = Cairo::LinearGradient::create(0, 0, 0, height);
  fill->add_color_stop_rgba(0, base.get_red(), base.get_green(), base.get_blue(), 1);
  fill->add_color_stop_rgba(1, bg.get_red(), bg.get_green(), bg.get_blue(), 1);
  context->set_source(fill);
  rounded(0, 0, width, height);
  context->fill();
  context->set_source_rgba(theme_fg.get_red(), theme_fg.get_green(), theme_fg.get_blue(), 0.22);
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
                           PulseClient& pulse, PrivSession& session,
                           const std::function<void(const std::string&)>& open_page,
                           const std::function<void(const std::string&)>& mark_page)
    : appearance_(appearance),
      sound_(sound),
      users_(users),
      pulse_(pulse),
      session_(session),
      open_page_(open_page),
      mark_page_(mark_page),
      volume_(Gtk::ORIENTATION_HORIZONTAL) {
  auto* page = make_vbox(0);
  page->set_border_width(0);

  auto* hero = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  hero->get_style_context()->add_class("lcos-card");
  hero->signal_draw().connect(
      [hero](const Cairo::RefPtr<Cairo::Context>& context) {
        paint_system_card(context, *hero, hero->get_allocated_width(), hero->get_allocated_height());
        return false;
      },
      false);
  auto* hero_row = Gtk::manage(new Gtk::FlowBox());
  hero_row->set_selection_mode(Gtk::SELECTION_NONE);
  hero_row->set_homogeneous(false);
  hero_row->set_min_children_per_line(1);
  hero_row->set_max_children_per_line(2);
  hero_row->set_column_spacing(18);
  hero_row->set_row_spacing(12);
  hero_row->set_hexpand(true);
  hero_row->set_halign(Gtk::ALIGN_FILL);

  auto* identity = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 18));
  identity->set_hexpand(true);
  logo_ = Gtk::manage(new Gtk::Image());
  logo_->set_size_request(kLogoSize, kLogoSize);
  logo_->set_valign(Gtk::ALIGN_CENTER);
  logo_->property_scale_factor().signal_changed().connect([this]() { refresh_host(); });
  identity->pack_start(*logo_, Gtk::PACK_SHRINK);

  auto* info = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
  info->set_valign(Gtk::ALIGN_CENTER);
  info->set_hexpand(true);
  kicker_ = Gtk::manage(new Gtk::Label());
  kicker_->set_halign(Gtk::ALIGN_FILL);
  kicker_->set_hexpand(true);
  kicker_->set_line_wrap(true);
  kicker_->set_lines(2);
  kicker_->set_ellipsize(Pango::ELLIPSIZE_END);
  kicker_->set_xalign(0);
  kicker_->get_style_context()->add_class("lcos-kicker");
  version_ = Gtk::manage(new Gtk::Label());
  version_->set_halign(Gtk::ALIGN_FILL);
  version_->set_hexpand(true);
  version_->set_line_wrap(false);
  version_->set_ellipsize(Pango::ELLIPSIZE_END);
  version_->set_xalign(0);
  version_->get_style_context()->add_class("lcos-version");
  auto* host_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  host_row->set_hexpand(true);
  host_row->pack_start(*icon_image("computer", 16), Gtk::PACK_SHRINK);
  hostname_ = Gtk::manage(new Gtk::Label());
  hostname_->set_halign(Gtk::ALIGN_START);
  hostname_->set_line_wrap(true);
  hostname_->set_lines(2);
  hostname_->set_ellipsize(Pango::ELLIPSIZE_END);
  hostname_->set_hexpand(false);
  hostname_->get_style_context()->add_class("lcos-host");
  auto* rename = Gtk::manage(new Gtk::Button("Rename…"));
  rename->set_relief(Gtk::RELIEF_NONE);
  rename->set_valign(Gtk::ALIGN_CENTER);
  rename->get_style_context()->add_class("lcos-link-button");
  rename->set_tooltip_text("Change the name of this computer");
  rename->signal_clicked().connect(sigc::mem_fun(*this, &OverviewPage::rename_host));
  host_row->pack_start(*hostname_, Gtk::PACK_SHRINK);
  host_row->pack_start(*rename, Gtk::PACK_SHRINK);
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
    (*values[i])->set_halign(Gtk::ALIGN_FILL);
    (*values[i])->set_hexpand(true);
    (*values[i])->set_line_wrap(false);
    (*values[i])->set_line_wrap(true);
    (*values[i])->set_lines(2);
    (*values[i])->set_ellipsize(Pango::ELLIPSIZE_END);
    (*values[i])->set_xalign(0);
    (*values[i])->get_style_context()->add_class("lcos-fact");
    facts->attach(*cap, 0, i, 1, 1);
    facts->attach(**values[i], 1, i, 1, 1);
  }
  info->set_size_request(120, -1);
  info->pack_start(*facts, Gtk::PACK_SHRINK);
  identity->pack_start(*info, Gtk::PACK_EXPAND_WIDGET);
  hero_row->insert(*identity, -1);

  auto* quick = Gtk::manage(new Gtk::Grid());
  quick->set_column_spacing(10);
  quick->set_row_spacing(8);
  quick->set_valign(Gtk::ALIGN_CENTER);
  quick->set_halign(Gtk::ALIGN_END);
  quick->set_hexpand(false);
  quick->set_size_request(180, -1);
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
  auto* vol = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  vol->set_hexpand(true);
  vol->get_style_context()->add_class("lcos-volume");
  volume_mute_ = Gtk::manage(new Gtk::ToggleButton());
  volume_mute_->set_image(*icon_image("audio-volume-high", 16));
  volume_mute_->set_relief(Gtk::RELIEF_NONE);
  volume_mute_->set_tooltip_text("Mute");
  volume_mute_->set_valign(Gtk::ALIGN_CENTER);
  vol->pack_start(*volume_mute_, Gtk::PACK_SHRINK);
  volume_.set_range(0, 100);
  volume_.set_digits(0);
  volume_.set_draw_value(false);
  volume_.set_hexpand(true);
  volume_.set_valign(Gtk::ALIGN_CENTER);
  volume_value_ = Gtk::manage(new Gtk::Label("0%"));
  volume_value_->set_width_chars(4);
  volume_value_->set_xalign(1);
  volume_value_->get_style_context()->add_class("lcos-fact");
  vol->pack_start(volume_, Gtk::PACK_EXPAND_WIDGET);
  vol->pack_start(*volume_value_, Gtk::PACK_SHRINK);
  quick->attach(*vol, 0, 3, 2, 1);
  hero_row->insert(*quick, -1);
  hero->pack_start(*hero_row, Gtk::PACK_SHRINK);
  page->pack_start(*hero, Gtk::PACK_SHRINK);

  const char* sections[] = {"Look and Feel", "Hardware", "System"};
  for (const char* section : sections) {
    auto* tiles = Gtk::manage(new Gtk::FlowBox());
    tiles->set_hexpand(true);
    tiles->set_homogeneous(true);
    tiles->set_selection_mode(Gtk::SELECTION_NONE);
    tiles->set_min_children_per_line(2);
    tiles->set_max_children_per_line(6);
    tiles->set_column_spacing(6);
    tiles->set_row_spacing(6);
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
      label->set_justify(Gtk::JUSTIFY_CENTER);
      label->set_width_chars(12);
      label->set_max_width_chars(16);
      label->set_ellipsize(Pango::ELLIPSIZE_NONE);
      label->get_style_context()->add_class("lcos-tile-label");
      box->pack_start(*image, Gtk::PACK_SHRINK);
      box->pack_start(*label, Gtk::PACK_SHRINK);
      button->add(*box);
      const std::string id = tile.id;
      button->signal_clicked().connect([this, id] { open(id); });
      tiles->insert(*button, -1);
      ++column;
    }
    (void)column;
    auto* frame = make_frame(section, *tiles);
    frame->set_margin_top(14);
    page->pack_start(*frame, Gtk::PACK_SHRINK);
  }
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  dark_.set_size_request(48, 22);
  autologin_.set_size_request(48, 22);
  if (dark_.get_accessible()) {
    dark_.get_accessible()->set_name("Dark style");
  }
  if (autologin_.get_accessible()) {
    autologin_.get_accessible()->set_name("Automatic login");
  }
  dark_.property_active().signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    appearance_.set_dark_style(dark_.get_active());
  });
  autologin_.property_active().signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    users_.set_automatic_keep(autologin_.get_active());
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
    if (volume_value_ != nullptr) {
      volume_value_->set_text(std::to_string(static_cast<int>(volume_.get_value())) + "%");
    }
    if (freeze_ > 0) {
      return;
    }
    sound_.set_output_volume_percent(static_cast<int>(volume_.get_value()));
  });
  volume_mute_->signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    sound_.set_output_muted(volume_mute_->get_active());
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

void OverviewPage::rename_host() {
  auto* parent = dynamic_cast<Gtk::Window*>(get_toplevel());
  Gtk::Dialog dialog("Rename this computer", true);
  if (parent != nullptr && parent != &dialog) {
    dialog.set_transient_for(*parent);
  }
  dialog.set_modal(true);
  dialog.set_default_size(420, -1);
  auto* box = dialog.get_content_area();
  box->set_margin_top(12);
  box->set_margin_bottom(6);
  box->set_margin_start(16);
  box->set_margin_end(16);
  box->set_spacing(8);
  auto* note = Gtk::manage(new Gtk::Label(
      "Letters, digits, and hyphens. Separate extra parts with a dot. At most 64 characters."));
  note->set_line_wrap(true);
  note->set_max_width_chars(46);
  note->set_halign(Gtk::ALIGN_START);
  note->set_xalign(0);
  auto* entry = Gtk::manage(new Gtk::Entry());
  const std::string current = hostname_ != nullptr ? std::string(hostname_->get_text()) : std::string();
  entry->set_text(current);
  entry->set_activates_default(true);
  auto* error = Gtk::manage(new Gtk::Label(""));
  error->set_halign(Gtk::ALIGN_START);
  error->set_xalign(0);
  error->get_style_context()->add_class("lcos-faint");
  box->pack_start(*note, Gtk::PACK_SHRINK);
  box->pack_start(*entry, Gtk::PACK_SHRINK);
  box->pack_start(*error, Gtk::PACK_SHRINK);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Rename", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_OK);
  entry->signal_changed().connect([&] {
    const std::string name = entry->get_text();
    const bool unchanged = name == current;
    const bool ok = valid_hostname(name) && !unchanged;
    dialog.set_response_sensitive(Gtk::RESPONSE_OK, ok);
    if (name.empty() || unchanged) {
      error->set_text("");
    } else if (!valid_hostname(name)) {
      error->set_text("That name is not a valid hostname.");
    } else {
      error->set_text("");
    }
  });
  dialog.show_all();
  dialog.set_response_sensitive(Gtk::RESPONSE_OK, false);
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  const std::string name = entry->get_text();
  dialog.hide();
  if (!valid_hostname(name) || name == current) {
    return;
  }
  const PrivResult result = session_.run({"set-hostname", name});
  if (!result.ok) {
    if (authentication_was_dismissed(result.error)) {
      return;
    }
    const std::string message =
        result.error.empty() ? "The name was not changed." : result.error;
    Gtk::MessageDialog alert("Could not rename this computer", false, Gtk::MESSAGE_ERROR,
                             Gtk::BUTTONS_OK, true);
    if (parent != nullptr) {
      alert.set_transient_for(*parent);
    }
    alert.set_title("Rename this computer");
    alert.set_secondary_text(message);
    alert.run();
    return;
  }
  refresh_host();
  Gtk::MessageDialog renamed("The name was changed", false, Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK, true);
  if (parent != nullptr) {
    renamed.set_transient_for(*parent);
  }
  renamed.set_title("Rename this computer");
  renamed.set_secondary_text(
      "Some programs keep the previous name until you log out or restart the computer.");
  renamed.run();
}

void OverviewPage::refresh_host() {
  const HostFacts facts = read_host_facts();
  if (logo_ != nullptr) {
    const int scale = std::max(1, logo_->get_scale_factor());
    if (auto pixbuf = branding_logo(facts.lcos, kLogoSize, scale)) {
      // A scaled surface keeps the logical size at kLogoSize on HiDPI while
      // drawing every device pixel from the full-resolution icon.
      logo_->set(Gdk::Cairo::create_surface_from_pixbuf(pixbuf, scale));
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
    if (volume_value_ != nullptr) {
      volume_value_->set_text(std::to_string(percent) + "%");
    }
  }
  if (volume_mute_ != nullptr) {
    volume_mute_->set_active(sound_.output_muted());
    volume_mute_->set_sensitive(sound_.output_ready());
    volume_mute_->set_tooltip_text(volume_mute_->get_active() ? "Unmute" : "Mute");
  }
  volume_.set_sensitive(sound_.output_ready());
}

bool OverviewPage::discard_pending() {
  if (!session_.action_open("set-hostname")) {
    return false;
  }
  const PrivResult result = session_.revert_action("set-hostname");
  if (!result.ok) {
    auto* parent = dynamic_cast<Gtk::Window*>(get_toplevel());
    Gtk::MessageDialog alert(result.error.empty() ? "The name could not be restored." : result.error,
                             false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
    if (parent != nullptr) {
      alert.set_transient_for(*parent);
    }
    alert.set_title("Rename this computer");
    alert.run();
    return false;
  }
  refresh_host();
  return true;
}

}  // namespace lcos
