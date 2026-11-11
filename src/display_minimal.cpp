// SPDX-License-Identifier: GPL-3.0-or-later

#include "display_minimal.hpp"

#include "logic/display_layout.hpp"
#include "logic/names.hpp"
#include "ui_util.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/messagedialog.h>

namespace lcos {

DisplayMinimal::DisplayMinimal(XfconfStore& store, XrandrBackend& randr, std::function<void()> open_full)
    : store_(store), randr_(randr), open_full_(std::move(open_full)) {
  set_title("Displays");
  set_default_size(460, 220);
  set_icon_name("org.lunduke.LcosSettings");
  auto* box = make_vbox(10);
  box->set_border_width(16);
  auto* note = make_label(
      "A display was connected. Extend the desktop, mirror the displays, or open Command Center.");
  note->set_line_wrap(true);
  note->set_max_width_chars(48);
  box->pack_start(*note, Gtk::PACK_SHRINK);
  if (!randr_.available()) {
    auto* missing = make_label(randr_.error().empty() ? "The display server did not answer."
                                                      : randr_.error());
    missing->set_line_wrap(true);
    box->pack_start(*missing, Gtk::PACK_SHRINK);
  }
  auto* row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  auto* extend = Gtk::manage(new Gtk::Button("_Extend", true));
  auto* mirror = Gtk::manage(new Gtk::Button("_Mirror", true));
  auto* open = Gtk::manage(new Gtk::Button("_Open Command Center", true));
  extend->signal_clicked().connect([this] { apply_layout(false); });
  mirror->signal_clicked().connect([this] { apply_layout(true); });
  open->signal_clicked().connect([this] {
    if (open_full_) {
      open_full_();
    }
  });
  row->pack_start(*extend, Gtk::PACK_SHRINK);
  row->pack_start(*mirror, Gtk::PACK_SHRINK);
  row->pack_end(*open, Gtk::PACK_SHRINK);
  box->pack_end(*row, Gtk::PACK_SHRINK);
  add(*box);
  show_all();
}

void DisplayMinimal::apply_layout(bool mirror) {
  if (!randr_.available()) {
    return;
  }
  auto outputs = randr_.read();
  if (outputs.empty()) {
    Gtk::MessageDialog dialog(*this, "There is no display to change.", false, Gtk::MESSAGE_WARNING,
                              Gtk::BUTTONS_OK, true);
    dialog.run();
    return;
  }
  if (mirror) {
    arrange_mirror(outputs);
  } else {
    arrange_extend(outputs);
  }
  bool primary = false;
  for (const auto& output : outputs) {
    primary = primary || (output.active && output.primary);
  }
  if (!primary) {
    for (auto& output : outputs) {
      if (output.active) {
        output.primary = true;
        break;
      }
    }
  }
  const std::string error = randr_.apply(outputs);
  if (!error.empty()) {
    Gtk::MessageDialog dialog(*this, "Could not apply the display layout.", false, Gtk::MESSAGE_ERROR,
                              Gtk::BUTTONS_OK, true);
    dialog.set_secondary_text(error);
    dialog.run();
    return;
  }
  for (const auto& output : outputs) {
    const auto path = [&](const std::string& leaf) { return display_prop("Default", output.name, leaf); };
    store_.set_string("displays", path(""), output.friendly.empty() ? output.name : output.friendly);
    store_.set_bool("displays", path("Active"), output.active);
    if (!output.edid.empty()) {
      store_.set_string("displays", path("EDID"), output.edid);
    }
    if (!output.active) {
      continue;
    }
    store_.set_string("displays", path("Resolution"), format_resolution(output.width, output.height));
    store_.set_double("displays", path("RefreshRate"), output.refresh);
    store_.set_int("displays", path("Rotation"), output.rotation);
    store_.set_string("displays", path("Reflection"), output.reflection.empty() ? "0" : output.reflection);
    store_.set_bool("displays", path("Primary"), output.primary);
    const double scalex = xfconf_scale_from_ui(output.scale);
    store_.set_double("displays", path("Scale"), scalex);
    store_.set_double("displays", path("Scale/X"), scalex);
    store_.set_double("displays", path("Scale/Y"), scalex);
    store_.set_int("displays", path("Position/X"), output.x);
    store_.set_int("displays", path("Position/Y"), output.y);
  }
  store_.set_string("displays", "/ActiveProfile", "Default");
  store_.drop_channel("displays");
  store_.set_string_untracked("displays", "/Schemes/Apply", "Default");
  hide();
}

}  // namespace lcos
