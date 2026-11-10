// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/appearance_page.hpp"

#include "logic/themes.hpp"
#include "ui_util.hpp"

#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

namespace lcos {

AppearancePage::AppearancePage(XfconfStore& store) : store_(store) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  auto* page = make_vbox(4);
  page->set_border_width(12);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  auto* style = make_vbox(8);
  FormGrid style_form;
  style_form.add("Widget theme", gtk_theme_);
  style_form.add("Icons", icon_theme_);
  style_form.add("Window manager", xfwm_theme_);
  sync_themes_.set_label("Use the widget theme for window borders when it includes one");
  style_form.add_full(sync_themes_);
  style->pack_start(style_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Style", *style), Gtk::PACK_SHRINK);

  auto* fonts = make_vbox(8);
  FormGrid font_form;
  font_form.add("Default font", font_);
  font_form.add("Monospace font", mono_);
  font_form.add("Title bar font", title_font_);
  title_align_.append("left", "Left");
  title_align_.append("center", "Center");
  title_align_.append("right", "Right");
  font_form.add("Title alignment", title_align_);
  fonts->pack_start(font_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Fonts", *fonts), Gtk::PACK_SHRINK);

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
  });
  icon_theme_.signal_changed().connect([this] {
    if (freeze_ > 0 || icon_theme_.get_active_id().empty()) {
      return;
    }
    store_.set_string("xsettings", "/Net/IconThemeName", icon_theme_.get_active_id());
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
        dpi = 96;
      }
      dpi_.set_value(dpi);
      store_.set_int("xsettings", "/Xft/DPI", dpi);
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
    store_.set_int("xsettings", "/Xft/DPI", value);
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
  fill_themes(gtk_theme_, scan_gtk_themes(data_roots("themes", true)), gtk);
  fill_themes(icon_theme_, scan_icon_themes(data_roots("icons", true)), icons);
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
  const int dpi = store_.get_int("xsettings", "/Xft/DPI", -1);
  custom_dpi_.set_active(dpi > 0);
  dpi_.set_sensitive(dpi > 0);
  dpi_.set_value(dpi > 0 ? dpi : store_.get_int("xsettings", "/Xfce/LastCustomDPI", 96));
  const std::string toolbar = store_.get_string("xsettings", "/Gtk/ToolbarStyle", "both-horiz");
  toolbar_.set_active_id(toolbar.empty() ? "both-horiz" : toolbar);
  menu_images_.set_active(store_.get_bool("xsettings", "/Gtk/MenuImages", true));
  button_images_.set_active(store_.get_bool("xsettings", "/Gtk/ButtonImages", true));
  edit_accels_.set_active(store_.get_bool("xsettings", "/Gtk/CanChangeAccels", false));
  header_buttons_.set_active(store_.get_bool("xsettings", "/Gtk/DialogsUseHeader", false));
  const int scale = store_.get_int("xsettings", "/Gdk/WindowScalingFactor", 1);
  window_scale_.set_active_id(scale >= 2 ? "2" : "1");
}

}  // namespace lcos
