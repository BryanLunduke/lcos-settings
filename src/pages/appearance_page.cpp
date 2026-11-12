// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/appearance_page.hpp"

#include "logic/helper_status.hpp"
#include "logic/themes.hpp"
#include "ui_util.hpp"

#include <pango/pangocairo.h>

#include <gtkmm/button.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/overlay.h>
#include <gtkmm/scrolledwindow.h>

#include <algorithm>
#include <cctype>

namespace lcos {
namespace {

bool name_is_dark(const std::string& text) {
  std::string lower = text;
  for (char& c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return lower.find("dark") != std::string::npos;
}

bool name_is_contrast(const std::string& text) {
  std::string lower = text;
  for (char& c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return lower.find("contrast") != std::string::npos;
}

void split_font(const std::string& spec, std::string& family, int& size) {
  size = 10;
  family = spec;
  const auto space = spec.rfind(' ');
  if (space == std::string::npos || space == 0) {
    return;
  }
  try {
    size = std::stoi(spec.substr(space + 1));
    family = spec.substr(0, space);
  } catch (const std::exception&) {
    family = spec;
    size = 10;
  }
  if (size < 6 || size > 96) {
    size = 10;
  }
}

class StyleSwatch : public Gtk::DrawingArea {
 public:
  StyleSwatch(bool dark, bool contrast) : dark_(dark), contrast_(contrast) {
    set_size_request(-1, 88);
  }

 protected:
  bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override {
    const double w = std::max(1, get_allocated_width());
    const double h = std::max(1, get_allocated_height());
    const double bg_r = contrast_ ? 0 : (dark_ ? 0.16 : 0.93);
    const double bg_g = contrast_ ? 0 : (dark_ ? 0.17 : 0.92);
    const double bg_b = contrast_ ? 0 : (dark_ ? 0.19 : 0.90);
    const double fg = contrast_ ? 1.0 : (dark_ ? 0.90 : 0.16);
    const double bar = contrast_ ? 0.05 : (dark_ ? 0.28 : 0.55);
    cr->set_source_rgb(bg_r, bg_g, bg_b);
    cr->rectangle(0, 0, w, h);
    cr->fill();
    cr->set_source_rgb(bar, dark_ ? 0.42 : 0.68, dark_ ? 0.62 : 0.82);
    cr->rectangle(0, 0, w, 14);
    cr->fill();
    cr->set_source_rgba(fg, fg, fg, dark_ ? 0.35 : 0.18);
    cr->rectangle(8, 26, 36, 8);
    cr->fill();
    cr->rectangle(8, 40, 28, 8);
    cr->fill();
    cr->set_source_rgba(fg, fg, fg, dark_ ? 0.55 : 0.28);
    cr->rectangle(52, 28, w - 64, 6);
    cr->fill();
    cr->rectangle(52, 40, w - 80, 6);
    cr->fill();
    cr->rectangle(52, 52, w - 96, 6);
    cr->fill();
    cr->set_source_rgb(dark_ ? 0.45 : 0.55, dark_ ? 0.62 : 0.72, dark_ ? 0.78 : 0.84);
    cr->rectangle(w - 48, h - 22, 36, 12);
    cr->fill();
    return true;
  }

 private:
  bool dark_ = false;
  bool contrast_ = false;
};

Gtk::Button* make_card(Gtk::Widget& body, const Glib::ustring& title, const Glib::ustring& meta,
                       bool selected) {
  auto* button = Gtk::manage(new Gtk::Button());
  button->set_relief(Gtk::RELIEF_NONE);
  button->get_style_context()->add_class("lcos-tcard");
  if (selected) {
    button->get_style_context()->add_class("lcos-tcard-selected");
  }
  auto* wrap = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  body.set_hexpand(true);
  wrap->pack_start(body, Gtk::PACK_SHRINK);
  auto* cap = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  cap->set_margin_top(6);
  auto* name = Gtk::manage(new Gtk::Label(title));
  name->set_halign(Gtk::ALIGN_START);
  name->set_ellipsize(Pango::ELLIPSIZE_END);
  name->set_hexpand(true);
  cap->pack_start(*name, Gtk::PACK_EXPAND_WIDGET);
  if (!meta.empty()) {
    auto* note = Gtk::manage(new Gtk::Label(meta));
    note->get_style_context()->add_class("lcos-faint");
    note->set_halign(Gtk::ALIGN_END);
    cap->pack_start(*note, Gtk::PACK_SHRINK);
  }
  wrap->pack_start(*cap, Gtk::PACK_SHRINK);
  auto* overlay = Gtk::manage(new Gtk::Overlay());
  overlay->add(*wrap);
  if (selected) {
    auto* badge = Gtk::manage(new Gtk::Label());
    badge->set_markup("<span foreground='#ffffff' weight='bold'>✓</span>");
    badge->get_style_context()->add_class("lcos-check");
    badge->set_halign(Gtk::ALIGN_END);
    badge->set_valign(Gtk::ALIGN_START);
    badge->set_margin_top(2);
    badge->set_margin_end(2);
    overlay->add_overlay(*badge);
  }
  button->add(*overlay);
  return button;
}

void clear_flow(Gtk::FlowBox& flow) {
  std::vector<Gtk::Widget*> children;
  flow.foreach ([&](Gtk::Widget& child) { children.push_back(&child); });
  for (auto* child : children) {
    flow.remove(*child);
    delete child;
  }
}

}  // namespace

AppearancePage::AppearancePage(XfconfStore& store)
    : store_(store), size_buttons_(Gtk::ORIENTATION_HORIZONTAL, 0) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  auto* page = make_vbox(4);
  page->set_border_width(12);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  auto* style = make_vbox(8);
  style_cards_.set_selection_mode(Gtk::SELECTION_NONE);
  style_cards_.set_homogeneous(true);
  style_cards_.set_column_spacing(12);
  style_cards_.set_row_spacing(12);
  style_cards_.set_min_children_per_line(2);
  style_cards_.set_max_children_per_line(3);
  style->pack_start(style_cards_, Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Style", *style), Gtk::PACK_SHRINK);

  auto* icons = make_vbox(8);
  icon_cards_.set_selection_mode(Gtk::SELECTION_NONE);
  icon_cards_.set_homogeneous(true);
  icon_cards_.set_column_spacing(12);
  icon_cards_.set_row_spacing(12);
  icon_cards_.set_min_children_per_line(2);
  icon_cards_.set_max_children_per_line(2);
  icons->pack_start(icon_cards_, Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Icons", *icons), Gtk::PACK_SHRINK);

  auto* fonts = make_vbox(8);
  font_cards_.set_selection_mode(Gtk::SELECTION_NONE);
  font_cards_.set_homogeneous(true);
  font_cards_.set_column_spacing(12);
  font_cards_.set_row_spacing(12);
  font_cards_.set_min_children_per_line(2);
  font_cards_.set_max_children_per_line(4);
  fonts->pack_start(font_cards_, Gtk::PACK_SHRINK);
  size_buttons_.get_style_context()->add_class("lcos-linked");
  size_buttons_.set_halign(Gtk::ALIGN_START);
  const int sizes[] = {9, 10, 11, 12};
  for (int i = 0; i < 4; ++i) {
    auto* button = Gtk::manage(new Gtk::Button(std::to_string(sizes[i])));
    font_size_buttons_[i] = button;
    button->signal_clicked().connect([this, size = sizes[i]] { set_default_size(size); });
    size_buttons_.pack_start(*button, Gtk::PACK_SHRINK);
  }
  auto* size_line = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  size_line->pack_start(size_buttons_, Gtk::PACK_SHRINK);
  auto* more = Gtk::manage(new Gtk::Button("Fonts…"));
  more->set_halign(Gtk::ALIGN_END);
  more->signal_clicked().connect(sigc::mem_fun(*this, &AppearancePage::show_font_dialog));
  size_line->pack_end(*more, Gtk::PACK_SHRINK);
  fonts->pack_start(*size_line, Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Fonts", *fonts), Gtk::PACK_SHRINK);

  auto* borders = make_vbox(8);
  FormGrid border_form;
  border_form.add("Window manager", xfwm_theme_);
  sync_themes_.set_label("Use the widget theme for window borders when it includes one");
  border_form.add_full(sync_themes_);
  borders->pack_start(border_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Window borders", *borders), Gtk::PACK_SHRINK);

  title_align_.append("left", "Left");
  title_align_.append("center", "Center");
  title_align_.append("right", "Right");

  auto* rendering = make_vbox(8);
  antialias_.append("-1", "Default");
  antialias_.append("1", "Enabled");
  antialias_.append("0", "Disabled");
  hinting_.append("hintnone", "None");
  hinting_.append("hintslight", "Slight");
  hinting_.append("hintmedium", "Medium");
  hinting_.append("hintfull", "Full");
  rgba_.append("none", "None");
  rgba_.append("rgb", "RGB");
  rgba_.append("bgr", "BGR");
  rgba_.append("vrgb", "Vertical RGB");
  rgba_.append("vbgr", "Vertical BGR");
  dpi_.set_range(50, 500);
  dpi_.set_increments(1, 10);
  dpi_.set_digits(0);
  FormGrid render_form;
  render_form.add("Antialiasing", antialias_);
  render_form.add("Hinting", hinting_);
  render_form.add("Subpixel order", rgba_);
  custom_dpi_.set_label("Custom DPI");
  render_form.add_full(custom_dpi_);
  render_form.add("DPI", dpi_);
  rendering->pack_start(render_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Font rendering", *rendering), Gtk::PACK_SHRINK);

  auto* interface = make_vbox(8);
  toolbar_.append("icons", "Icons only");
  toolbar_.append("text", "Text only");
  toolbar_.append("both", "Text under icons");
  toolbar_.append("both-horiz", "Text beside icons");
  window_scale_.append("1", "1×");
  window_scale_.append("2", "2×");
  menu_images_.set_label("Show images in menus");
  button_images_.set_label("Show images in buttons");
  edit_accels_.set_label("Let menus be edited with the keyboard");
  header_buttons_.set_label("Put buttons in dialog headers");
  FormGrid ui_form;
  ui_form.add("Toolbar style", toolbar_);
  ui_form.add("Window scaling", window_scale_);
  ui_form.add_full(menu_images_);
  ui_form.add_full(button_images_);
  ui_form.add_full(edit_accels_);
  ui_form.add_full(header_buttons_);
  interface->pack_start(ui_form.grid(), Gtk::PACK_SHRINK);
  auto* note = make_label(
      "Window scaling is the GTK integer factor. Per-display RandR scale is on the Display page.");
  note->set_line_wrap(true);
  interface->pack_start(*note, Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Interface", *interface), Gtk::PACK_SHRINK);

  gtk_theme_.signal_changed().connect([this] {
    if (freeze_ > 0 || gtk_theme_.get_active_id().empty()) {
      return;
    }
    store_.set_string("xsettings", "/Net/ThemeName", gtk_theme_.get_active_id());
    sync_xfwm_if_needed();
    schedule_cards();
  });
  icon_theme_.signal_changed().connect([this] {
    if (freeze_ > 0 || icon_theme_.get_active_id().empty()) {
      return;
    }
    store_.set_string("xsettings", "/Net/IconThemeName", icon_theme_.get_active_id());
    schedule_cards();
  });
  xfwm_theme_.signal_changed().connect([this] {
    if (freeze_ > 0 || xfwm_theme_.get_active_id().empty()) {
      return;
    }
    store_.set_string("xfwm4", "/general/theme", xfwm_theme_.get_active_id());
  });
  sync_themes_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool("xsettings", "/Xfce/SyncThemes", sync_themes_.get_active());
    sync_xfwm_if_needed();
  });
  font_.signal_font_set().connect([this] {
    if (freeze_ == 0) {
      store_.set_string("xsettings", "/Gtk/FontName", font_.get_font_name());
      schedule_cards();
    }
  });
  mono_.signal_font_set().connect([this] {
    if (freeze_ == 0) {
      store_.set_string("xsettings", "/Gtk/MonospaceFontName", mono_.get_font_name());
    }
  });
  title_font_.signal_font_set().connect([this] {
    if (freeze_ == 0) {
      store_.set_string("xfwm4", "/general/title_font", title_font_.get_font_name());
    }
  });
  title_align_.signal_changed().connect([this] {
    if (freeze_ > 0 || title_align_.get_active_id().empty()) {
      return;
    }
    store_.set_string("xfwm4", "/general/title_alignment", title_align_.get_active_id());
  });
  antialias_.signal_changed().connect([this] {
    if (freeze_ > 0 || antialias_.get_active_id().empty()) {
      return;
    }
    store_.set_int("xsettings", "/Xft/Antialias", std::stoi(antialias_.get_active_id()));
  });
  hinting_.signal_changed().connect([this] {
    if (freeze_ > 0 || hinting_.get_active_id().empty()) {
      return;
    }
    const std::string style = hinting_.get_active_id();
    store_.set_string("xsettings", "/Xft/HintStyle", style);
    store_.set_int("xsettings", "/Xft/Hinting", style == "hintnone" ? 0 : 1);
  });
  rgba_.signal_changed().connect([this] {
    if (freeze_ > 0 || rgba_.get_active_id().empty()) {
      return;
    }
    store_.set_string("xsettings", "/Xft/RGBA", rgba_.get_active_id());
  });
  custom_dpi_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    dpi_.set_sensitive(custom_dpi_.get_active());
    if (custom_dpi_.get_active()) {
      int dpi = store_.get_int("xsettings", "/Xfce/LastCustomDPI", -1);
      if (dpi < 50) {
        dpi = xft_dpi_to_spin(store_.get_int("xsettings", "/Xft/DPI", -1));
      }
      if (dpi < 50) {
        dpi = 96;
      }
      dpi_.set_value(dpi);
      store_.set_int("xsettings", "/Xft/DPI", spin_to_xft_dpi(dpi));
      store_.set_int("xsettings", "/Xfce/LastCustomDPI", dpi);
    } else {
      store_.set_int("xsettings", "/Xfce/LastCustomDPI", dpi_.get_value_as_int());
      store_.set_int("xsettings", "/Xft/DPI", -1);
    }
  });
  dpi_.signal_value_changed().connect([this] {
    if (freeze_ > 0 || !custom_dpi_.get_active()) {
      return;
    }
    const int value = dpi_.get_value_as_int();
    store_.set_int("xsettings", "/Xft/DPI", spin_to_xft_dpi(value));
    store_.set_int("xsettings", "/Xfce/LastCustomDPI", value);
  });
  toolbar_.signal_changed().connect([this] {
    if (freeze_ > 0 || toolbar_.get_active_id().empty()) {
      return;
    }
    store_.set_string("xsettings", "/Gtk/ToolbarStyle", toolbar_.get_active_id());
  });
  menu_images_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      store_.set_bool("xsettings", "/Gtk/MenuImages", menu_images_.get_active());
    }
  });
  button_images_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      store_.set_bool("xsettings", "/Gtk/ButtonImages", button_images_.get_active());
    }
  });
  edit_accels_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      store_.set_bool("xsettings", "/Gtk/CanChangeAccels", edit_accels_.get_active());
    }
  });
  header_buttons_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      store_.set_bool("xsettings", "/Gtk/DialogsUseHeader", header_buttons_.get_active());
    }
  });
  window_scale_.signal_changed().connect([this] {
    if (freeze_ > 0 || window_scale_.get_active_id().empty()) {
      return;
    }
    store_.set_int("xsettings", "/Gdk/WindowScalingFactor",
                   std::stoi(window_scale_.get_active_id()));
  });

  load();
  show_all();
}

void AppearancePage::fill_themes(Gtk::ComboBoxText& combo,
                                 const std::vector<ThemeEntry>& themes,
                                 const std::string& current) {
  bool saw = false;
  for (const auto& theme : themes) {
    combo.append(theme.id, theme.name == theme.id ? theme.id : theme.name + "  (" + theme.id + ")");
    if (theme.id == current) {
      saw = true;
    }
  }
  if (!current.empty() && !saw) {
    combo.append(current, current);
  }
  if (!current.empty()) {
    combo.set_active_id(current);
  } else if (!themes.empty()) {
    combo.set_active(0);
  }
}

void AppearancePage::sync_xfwm_if_needed() {
  if (!sync_themes_.get_active() || gtk_theme_.get_active_id().empty()) {
    return;
  }
  const std::string theme = gtk_theme_.get_active_id();
  const auto xfwm = scan_xfwm_themes(data_roots("themes", true));
  for (const auto& entry : xfwm) {
    if (entry.id == theme) {
      Freeze freeze(freeze_);
      xfwm_theme_.set_active_id(theme);
      store_.set_string("xfwm4", "/general/theme", theme);
      return;
    }
  }
}

void AppearancePage::load() {
  Freeze freeze(freeze_);
  const std::string gtk = store_.get_string("xsettings", "/Net/ThemeName", "Adwaita");
  const std::string icons = store_.get_string("xsettings", "/Net/IconThemeName", "Adwaita");
  const std::string xfwm = store_.get_string("xfwm4", "/general/theme", "Default");
  gtk_themes_ = scan_gtk_themes(data_roots("themes", true));
  icon_themes_ = scan_icon_themes(data_roots("icons", true));
  fill_themes(gtk_theme_, gtk_themes_, gtk);
  fill_themes(icon_theme_, icon_themes_, icons);
  fill_themes(xfwm_theme_, scan_xfwm_themes(data_roots("themes", true)), xfwm);
  sync_themes_.set_active(store_.get_bool("xsettings", "/Xfce/SyncThemes", false));
  font_.set_font_name(store_.get_string("xsettings", "/Gtk/FontName", "Sans 10"));
  mono_.set_font_name(store_.get_string("xsettings", "/Gtk/MonospaceFontName", "Monospace 10"));
  title_font_.set_font_name(store_.get_string("xfwm4", "/general/title_font", "Sans Bold 9"));
  title_align_.set_active_id(store_.get_string("xfwm4", "/general/title_alignment", "center"));
  antialias_.set_active_id(std::to_string(store_.get_int("xsettings", "/Xft/Antialias", -1)));
  const std::string hint = store_.get_string("xsettings", "/Xft/HintStyle", "hintslight");
  hinting_.set_active_id(hint.empty() ? "hintslight" : hint);
  const std::string sub = store_.get_string("xsettings", "/Xft/RGBA", "none");
  rgba_.set_active_id(sub.empty() ? "none" : sub);
  const int stored_dpi = store_.get_int("xsettings", "/Xft/DPI", -1);
  const int dpi = xft_dpi_to_spin(stored_dpi);
  custom_dpi_.set_active(dpi > 0);
  dpi_.set_sensitive(dpi > 0);
  const int shown = dpi > 0 ? dpi : store_.get_int("xsettings", "/Xfce/LastCustomDPI", 96);
  dpi_.set_value(shown > 0 ? shown : 96);
  const std::string toolbar = store_.get_string("xsettings", "/Gtk/ToolbarStyle", "both-horiz");
  toolbar_.set_active_id(toolbar.empty() ? "both-horiz" : toolbar);
  menu_images_.set_active(store_.get_bool("xsettings", "/Gtk/MenuImages", true));
  button_images_.set_active(store_.get_bool("xsettings", "/Gtk/ButtonImages", true));
  edit_accels_.set_active(store_.get_bool("xsettings", "/Gtk/CanChangeAccels", false));
  header_buttons_.set_active(store_.get_bool("xsettings", "/Gtk/DialogsUseHeader", false));
  const int scale = store_.get_int("xsettings", "/Gdk/WindowScalingFactor", 1);
  window_scale_.set_active_id(scale >= 2 ? "2" : "1");
  std::string family;
  split_font(font_.get_font_name(), family, shown_font_size_);
  rebuild_cards();
}

AppearancePage::~AppearancePage() {
  if (cards_idle_.connected()) {
    cards_idle_.disconnect();
  }
  delete fonts_dialog_;
  fonts_dialog_ = nullptr;
}

bool AppearancePage::dark_style_available() const {
  for (const auto& theme : gtk_themes_) {
    if (name_is_dark(theme.id) || name_is_dark(theme.name)) {
      return true;
    }
  }
  return false;
}

bool AppearancePage::dark_style_on() const {
  return name_is_dark(gtk_theme_.get_active_id());
}

void AppearancePage::set_dark_style(bool on) {
  if (on == dark_style_on()) {
    return;
  }
  if (on) {
    const std::string current = gtk_theme_.get_active_id();
    if (!name_is_dark(current)) {
      light_theme_ = current;
    }
    std::string chosen;
    const std::string folded = current;
    for (const auto& theme : gtk_themes_) {
      if (!name_is_dark(theme.id) && !name_is_dark(theme.name)) {
        continue;
      }
      if (!folded.empty() && (theme.id.find(folded) != std::string::npos ||
                              folded.find(theme.id) != std::string::npos)) {
        chosen = theme.id;
        break;
      }
      if (chosen.empty()) {
        chosen = theme.id;
      }
    }
    if (!chosen.empty()) {
      gtk_theme_.set_active_id(chosen);
    }
    return;
  }
  std::string back = light_theme_;
  if (back.empty() || name_is_dark(back)) {
    back.clear();
    for (const auto& theme : gtk_themes_) {
      if (!name_is_dark(theme.id) && !name_is_dark(theme.name)) {
        back = theme.id;
        break;
      }
    }
  }
  if (!back.empty()) {
    gtk_theme_.set_active_id(back);
  }
}

void AppearancePage::schedule_cards() {
  if (cards_idle_.connected()) {
    return;
  }
  cards_idle_ = Glib::signal_idle().connect([this] {
    cards_idle_.disconnect();
    rebuild_cards();
    return false;
  });
}

void AppearancePage::set_default_family(const std::string& family) {
  font_.set_font_name(family + " " + std::to_string(shown_font_size_));
}

void AppearancePage::set_default_size(int size) {
  shown_font_size_ = size;
  std::string family;
  int ignored = 10;
  split_font(font_.get_font_name(), family, ignored);
  if (family.empty()) {
    family = "Sans";
  }
  font_.set_font_name(family + " " + std::to_string(size));
}

void AppearancePage::show_font_dialog() {
  if (fonts_dialog_ == nullptr) {
    auto* parent = dynamic_cast<Gtk::Window*>(get_toplevel());
    fonts_dialog_ = parent != nullptr ? new Gtk::Dialog("Fonts", *parent, true)
                                      : new Gtk::Dialog("Fonts", true);
    auto* grid_host = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 8));
    grid_host->set_margin_top(12);
    grid_host->set_margin_bottom(12);
    grid_host->set_margin_start(12);
    grid_host->set_margin_end(12);
    FormGrid form;
    form.add("Default font", font_);
    form.add("Monospace font", mono_);
    form.add("Title bar font", title_font_);
    form.add("Title alignment", title_align_);
    grid_host->pack_start(form.grid(), Gtk::PACK_SHRINK);
    fonts_dialog_->get_content_area()->pack_start(*grid_host, Gtk::PACK_SHRINK);
    fonts_dialog_->add_button("Close", Gtk::RESPONSE_CLOSE);
    fonts_dialog_->signal_response().connect([this](int) {
      if (fonts_dialog_ != nullptr) {
        fonts_dialog_->hide();
      }
    });
  }
  fonts_dialog_->show_all();
  fonts_dialog_->present();
}

void AppearancePage::rebuild_cards() {
  const std::string gtk = gtk_theme_.get_active_id();
  const std::string icons = icon_theme_.get_active_id();
  clear_flow(style_cards_);
  for (const auto& theme : gtk_themes_) {
    const bool selected = theme.id == gtk;
    auto* swatch = Gtk::manage(new StyleSwatch(name_is_dark(theme.id) || name_is_dark(theme.name),
                                               name_is_contrast(theme.id) || name_is_contrast(theme.name)));
    Glib::ustring meta;
    if (selected) {
      meta = "Default";
    } else if (theme.name != theme.id) {
      meta = theme.id;
    }
    auto* card = make_card(*swatch, theme.name, meta, selected);
    const std::string id = theme.id;
    card->signal_clicked().connect([this, id] {
      if (gtk_theme_.get_active_id() != id) {
        gtk_theme_.set_active_id(id);
      }
    });
    style_cards_.insert(*card, -1);
  }
  clear_flow(icon_cards_);
  for (const auto& theme : icon_themes_) {
    const bool selected = theme.id == icons;
    auto* row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
    row->set_margin_top(8);
    row->set_margin_bottom(8);
    row->set_margin_start(8);
    row->set_margin_end(8);
    row->set_halign(Gtk::ALIGN_CENTER);
    auto legacy = Gtk::IconTheme::create();
    legacy->set_custom_theme(theme.id);
    const char* names[] = {"folder", "text-x-generic", "computer", "image-x-generic",
                           "audio-x-generic"};
    for (const char* name : names) {
      auto* image = Gtk::manage(new Gtk::Image());
      image->set_pixel_size(32);
      try {
        auto pixbuf = legacy->load_icon(name, 32, 1, Gtk::ICON_LOOKUP_FORCE_SIZE);
        if (pixbuf) {
          image->set(pixbuf);
        }
      } catch (const Glib::Error&) {
      }
      row->pack_start(*image, Gtk::PACK_SHRINK);
    }
    auto* card = make_card(*row, theme.name, selected ? "Default" : "", selected);
    const std::string id = theme.id;
    card->signal_clicked().connect([this, id] {
      if (icon_theme_.get_active_id() != id) {
        icon_theme_.set_active_id(id);
      }
    });
    icon_cards_.insert(*card, -1);
  }

  std::string family;
  int size = 10;
  split_font(font_.get_font_name(), family, size);
  shown_font_size_ = size;
  std::vector<std::string> families;
  auto add_family = [&](const std::string& name) {
    if (name.empty()) {
      return;
    }
    if (std::find(families.begin(), families.end(), name) == families.end()) {
      families.push_back(name);
    }
  };
  auto map = Glib::wrap(pango_cairo_font_map_get_default(), true);
  std::vector<std::string> installed;
  if (map) {
    for (const auto& face : map->list_families()) {
      if (face) {
        installed.push_back(face->get_name());
      }
    }
  }
  auto exists = [&](const std::string& name) {
    return std::find(installed.begin(), installed.end(), name) != installed.end();
  };
  const char* preferred[] = {"DejaVu Sans", "Noto Sans", "Fira Sans", "Liberation Sans",
                             "Cantarell", "Ubuntu", "DejaVu Serif", "FreeSans"};
  if (exists(family) || family.empty()) {
    add_family(family.empty() ? "Sans" : family);
  } else {
    add_family(family);
  }
  for (const char* name : preferred) {
    if (exists(name)) {
      add_family(name);
    }
    if (families.size() >= 4) {
      break;
    }
  }
  for (const auto& name : installed) {
    if (families.size() >= 4) {
      break;
    }
    add_family(name);
  }
  clear_flow(font_cards_);
  for (const auto& name : families) {
    const bool selected = name == family;
    auto* preview = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
    preview->set_margin_top(8);
    preview->set_margin_bottom(6);
    preview->set_margin_start(10);
    preview->set_margin_end(10);
    auto* sample = Gtk::manage(new Gtk::Label("Aa"));
    sample->set_halign(Gtk::ALIGN_START);
    sample->override_font(Pango::FontDescription(name + " 28"));
    auto* line = Gtk::manage(new Gtk::Label("The quick brown fox"));
    line->set_halign(Gtk::ALIGN_START);
    line->set_ellipsize(Pango::ELLIPSIZE_END);
    line->override_font(Pango::FontDescription(name + " 11"));
    line->get_style_context()->add_class("lcos-dim");
    preview->pack_start(*sample, Gtk::PACK_SHRINK);
    preview->pack_start(*line, Gtk::PACK_SHRINK);
    auto* card = make_card(*preview, name, selected ? "Default" : "", selected);
    card->signal_clicked().connect([this, name] { set_default_family(name); });
    font_cards_.insert(*card, -1);
  }
  for (int i = 0; i < 4; ++i) {
    const int sizes[] = {9, 10, 11, 12};
    if (font_size_buttons_[i] == nullptr) {
      continue;
    }
    if (sizes[i] == shown_font_size_) {
      font_size_buttons_[i]->get_style_context()->add_class("lcos-on");
    } else {
      font_size_buttons_[i]->get_style_context()->remove_class("lcos-on");
    }
  }
  show_all();
}

}  // namespace lcos
