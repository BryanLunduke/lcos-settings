// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/display_page.hpp"

#include "logic/confirm.hpp"
#include "logic/names.hpp"
#include "logic/text.hpp"
#include "ui_util.hpp"

#include <cstdio>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <glibmm/main.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dialog.h>
#include <gdkmm/pixbuf.h>
#include <gtkmm/entry.h>
#include <gtkmm/scrollbar.h>
#include <gtkmm/label.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/popover.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace lcos {
namespace {

std::string output_ui_name(const OutputConfig& output) {
  if (output.name.rfind("eDP", 0) == 0 || output.name.rfind("LVDS", 0) == 0 ||
      output.name.rfind("DSI", 0) == 0) {
    return "Built-in Display";
  }
  if (!output.friendly.empty() && output.friendly != "Laptop") {
    return output.friendly;
  }
  return output.name;
}

std::string layout_names(const std::vector<OutputConfig>& layout) {
  if (layout.empty()) {
    return "the display layout";
  }
  std::string text;
  for (const auto& output : layout) {
    if (!text.empty()) {
      text += ", ";
    }
    const std::string friendly = output_ui_name(output);
    if (friendly == output.name) {
      text += output.name;
    } else {
      text += friendly + " (" + output.name + ")";
    }
  }
  return text;
}

std::string scale_percent(double scale) {
  return std::to_string(static_cast<int>(std::lround(scale * 100.0))) + "%";
}

std::string output_summary_line(const OutputConfig& output) {
  if (!output.active) {
    return output_ui_name(output) + "  off";
  }
  char rate[32];
  std::snprintf(rate, sizeof rate, "%.0f Hz", output.refresh);
  return output_ui_name(output) + "  " + std::to_string(output.width) + " × " +
         std::to_string(output.height) + " · " + rate + " · " + scale_percent(output.scale);
}

class CountdownRing : public Gtk::DrawingArea {
 public:
  CountdownRing() { set_size_request(84, 84); }
  void set_seconds(int seconds) {
    seconds_ = seconds;
    queue_draw();
  }

 protected:
  bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override {
    const double size = std::min(get_allocated_width(), get_allocated_height());
    const double cx = get_allocated_width() / 2.0;
    const double cy = get_allocated_height() / 2.0;
    const double radius = size / 2.0 - 6.0;
    cr->set_line_width(6);
    cr->set_source_rgb(0.82, 0.81, 0.78);
    cr->arc(cx, cy, radius, 0, 6.28318530718);
    cr->stroke();
    const double fraction =
        std::max(0.0, std::min(1.0, static_cast<double>(seconds_) / kDisplayConfirmSeconds));
    cr->set_source_rgb(0.435, 0.592, 0.788);
    cr->arc(cx, cy, radius, -1.57079632679, -1.57079632679 + fraction * 6.28318530718);
    cr->stroke();
    cr->set_source_rgb(0.184, 0.333, 0.498);
    cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
    cr->set_font_size(26);
    const std::string number = std::to_string(seconds_);
    Cairo::TextExtents extents;
    cr->get_text_extents(number, extents);
    cr->move_to(cx - extents.width / 2.0 - extents.x_bearing, cy - 2);
    cr->show_text(number);
    cr->set_source_rgb(0.55, 0.54, 0.51);
    cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
    cr->set_font_size(9);
    const std::string unit = "SECONDS";
    cr->get_text_extents(unit, extents);
    cr->move_to(cx - extents.width / 2.0 - extents.x_bearing, cy + 16);
    cr->show_text(unit);
    return true;
  }

 private:
  int seconds_ = kDisplayConfirmSeconds;
};

class ConfirmDialog : public Gtk::Dialog {
 public:
  ConfirmDialog(Gtk::Window& parent, const std::vector<OutputConfig>& outputs)
      : Gtk::Dialog("Keep this display configuration?", parent, true) {
    set_default_size(520, 230);
    get_style_context()->add_class("lcos-keep");
    auto* body = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 18));
    body->set_margin_top(24);
    body->set_margin_bottom(18);
    body->set_margin_start(24);
    body->set_margin_end(24);
    ring_.set_valign(Gtk::ALIGN_START);
    body->pack_start(ring_, Gtk::PACK_SHRINK);
    auto* text = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
    auto* title = Gtk::manage(new Gtk::Label("Keep this display configuration?"));
    title->set_halign(Gtk::ALIGN_START);
    title->get_style_context()->add_class("lcos-dialog-title");
    auto* explain = Gtk::manage(new Gtk::Label(
        "Your displays were changed. If you don't answer, LCOS restores the previous settings."));
    explain->set_halign(Gtk::ALIGN_START);
    explain->set_line_wrap(true);
    explain->set_max_width_chars(42);
    explain->set_xalign(0);
    text->pack_start(*title, Gtk::PACK_SHRINK);
    text->pack_start(*explain, Gtk::PACK_SHRINK);
    summary_.set_halign(Gtk::ALIGN_START);
    summary_.set_line_wrap(true);
    summary_.set_xalign(0);
    summary_.get_style_context()->add_class("lcos-faint");
    std::string lines;
    for (const auto& output : outputs) {
      if (!lines.empty()) {
        lines.push_back('\n');
      }
      lines += output_summary_line(output);
    }
    summary_.set_text(lines);
    text->pack_start(summary_, Gtk::PACK_SHRINK);
    body->pack_start(*text, Gtk::PACK_EXPAND_WIDGET);
    get_content_area()->pack_start(*body, Gtk::PACK_EXPAND_WIDGET);
    auto* revert = add_button("Revert", Gtk::RESPONSE_REJECT);
    auto* keep = add_button("Keep Changes", Gtk::RESPONSE_ACCEPT);
    (void)revert;
    keep->get_style_context()->add_class("lcos-primary");
    keep->set_can_default(true);
    set_default(*keep);
    set_keep_above(true);
    show_all();
    present();
    ring_.set_seconds(countdown_.seconds);
    timer_ = Glib::signal_timeout().connect_seconds(sigc::mem_fun(*this, &ConfirmDialog::tick), 1);
  }

  ~ConfirmDialog() override {
    if (timer_.connected()) {
      timer_.disconnect();
    }
  }

 private:
  bool tick() {
    countdown_.tick();
    ring_.set_seconds(countdown_.seconds);
    if (countdown_.decided) {
      response(countdown_.keep ? Gtk::RESPONSE_ACCEPT : Gtk::RESPONSE_REJECT);
      return false;
    }
    return true;
  }

  ConfirmCountdown countdown_;
  CountdownRing ring_;
  Gtk::Label summary_;
  sigc::connection timer_;
};

void rounded_rect(const Cairo::RefPtr<Cairo::Context>& context, double x, double y,
                  double width, double height, double radius) {
  context->begin_new_sub_path();
  constexpr double kHalfPi = 1.5707963267948966;
  constexpr double kPi = 3.141592653589793;
  context->arc(x + width - radius, y + radius, radius, -kHalfPi, 0);
  context->arc(x + width - radius, y + height - radius, radius, 0, kHalfPi);
  context->arc(x + radius, y + height - radius, radius, kHalfPi, kPi);
  context->arc(x + radius, y + radius, radius, kPi, 3 * kHalfPi);
  context->close_path();
}

}  // namespace

DisplayPage::DisplayPage(XfconfStore& store, XrandrBackend& randr, Gtk::Window& parent)
    : store_(store), randr_(randr), parent_(parent) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  auto* right = make_vbox(0);
  right->set_border_width(12);
  right->set_hexpand(true);
  scroll->add(*right);
  scroll_ = scroll;
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);
  signal_size_allocate().connect([this](Gtk::Allocation&) {
    if (paint_idle_.connected()) {
      return;
    }
    paint_idle_ = Glib::signal_idle().connect([this]() {
      if (alive_) {
        paint_now();
      }
      return false;
    });
  });

  arrange_.set_size_request(-1, 196);
  arrange_.set_hexpand(true);
  arrange_.get_style_context()->add_class("lcos-arrange");
  arrange_.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK |
                      Gdk::BUTTON_MOTION_MASK);
  arrange_.signal_draw().connect(sigc::mem_fun(*this, &DisplayPage::on_arrange_draw));
  arrange_.signal_button_press_event().connect(sigc::mem_fun(*this, &DisplayPage::on_button_press));
  arrange_.signal_motion_notify_event().connect(sigc::mem_fun(*this, &DisplayPage::on_motion));
  arrange_.signal_button_release_event().connect(
      sigc::mem_fun(*this, &DisplayPage::on_button_release));
  right->pack_start(arrange_, Gtk::PACK_SHRINK);

  auto* arrange_tools = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  arrange_tools->set_margin_top(12);
  auto* hint = make_label("Drag the displays to match how they sit on your desk.");
  hint->set_hexpand(true);
  hint->set_halign(Gtk::ALIGN_START);
  hint->get_style_context()->add_class("lcos-faint");
  auto* identify = Gtk::manage(new Gtk::Button("Identify"));
  extend_button_ = Gtk::manage(new Gtk::Button("Extend"));
  mirror_button_ = Gtk::manage(new Gtk::Button("Mirror"));
  auto* modes = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
  modes->get_style_context()->add_class("lcos-linked");
  modes->pack_start(*extend_button_, Gtk::PACK_SHRINK);
  modes->pack_start(*mirror_button_, Gtk::PACK_SHRINK);
  identify->signal_clicked().connect(sigc::mem_fun(*this, &DisplayPage::identify));
  extend_button_->signal_clicked().connect([this] {
    arrange_extend(outputs_);
    mark_dirty();
    show_selected();
    redraw();
  });
  mirror_button_->signal_clicked().connect([this] {
    arrange_mirror(outputs_);
    ensure_primary();
    mark_dirty();
    show_selected();
    redraw();
  });
  arrange_tools->pack_start(*hint, Gtk::PACK_EXPAND_WIDGET);
  arrange_tools->pack_start(*identify, Gtk::PACK_SHRINK);
  arrange_tools->pack_start(*modes, Gtk::PACK_SHRINK);
  right->pack_start(*arrange_tools, Gtk::PACK_SHRINK);

  auto* selected_group = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  selected_group->get_style_context()->add_class("lcos-group");
  selected_group->set_margin_top(18);
  auto* head = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
  output_heading_ = Gtk::manage(new Gtk::Label("Display"));
  output_heading_->set_halign(Gtk::ALIGN_START);
  output_heading_->set_ellipsize(Pango::ELLIPSIZE_END);
  output_heading_->get_style_context()->add_class("lcos-group-title");
  auto* rule = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
  rule->set_hexpand(true);
  rule->set_valign(Gtk::ALIGN_CENTER);
  head->pack_start(*output_heading_, Gtk::PACK_SHRINK);
  head->pack_start(*rule, Gtk::PACK_EXPAND_WIDGET);
  selected_group->pack_start(*head, Gtk::PACK_SHRINK);

  FormGrid form;
  primary_.set_label("Primary display");
  auto* use_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 18));
  enabled_.set_valign(Gtk::ALIGN_CENTER);
  enabled_.set_size_request(48, 22);
  use_row->pack_start(enabled_, Gtk::PACK_SHRINK);
  use_row->pack_start(primary_, Gtk::PACK_SHRINK);
  form.add("Use this display", *use_row);
  form.add("Resolution", resolution_);
  refresh_label_ = form.add("Refresh rate", refresh_);
  scale_.set_range(0.1, 10.0);
  scale_.set_increments(0.1, 1.0);
  scale_.set_digits(2);
  scale_.set_value(1.0);
  auto* scale_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  auto* linked = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
  linked->get_style_context()->add_class("lcos-linked");
  const double presets[] = {1.0, 1.25, 1.5, 1.75, 2.0};
  const char* preset_labels[] = {"100%", "125%", "150%", "175%", "200%"};
  for (int i = 0; i < 5; ++i) {
    scale_buttons_[i] = Gtk::manage(new Gtk::Button(preset_labels[i]));
    const double preset = presets[i];
    scale_buttons_[i]->signal_clicked().connect([this, preset] {
      scale_.set_value(preset);
    });
    linked->pack_start(*scale_buttons_[i], Gtk::PACK_SHRINK);
  }
  custom_scale_ = Gtk::manage(new Gtk::Button("Custom…"));
  scale_popover_ = Gtk::manage(new Gtk::Popover(*custom_scale_));
  auto* pop = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
  pop->set_margin_top(12);
  pop->set_margin_bottom(12);
  pop->set_margin_start(12);
  pop->set_margin_end(12);
  scale_.set_numeric(true);
  pop->pack_start(scale_, Gtk::PACK_SHRINK);
  scale_popover_->add(*pop);
  custom_scale_->signal_clicked().connect([this] { scale_popover_->popup(); });
  scale_row->pack_start(*linked, Gtk::PACK_SHRINK);
  scale_row->pack_end(*custom_scale_, Gtk::PACK_SHRINK);
  form.add("Scale", *scale_row);
  form.add("Rotation", rotation_);
  form.add("Reflection", reflection_);
  x_pos_.set_range(-8000, 16000);
  y_pos_.set_range(-8000, 16000);
  x_pos_.set_increments(1, 50);
  y_pos_.set_increments(1, 50);
  x_pos_.set_digits(0);
  y_pos_.set_digits(0);
  form.add("Position X", x_pos_);
  form.add("Position Y", y_pos_);
  form.grid().set_margin_start(12);
  form.grid().set_margin_end(8);
  form.grid().set_margin_top(12);
  selected_group->pack_start(form.grid(), Gtk::PACK_SHRINK);
  right->pack_start(*selected_group, Gtk::PACK_SHRINK);

  rotation_.append("0", "Normal");
  rotation_.append("90", "Clockwise");
  rotation_.append("180", "Upside down");
  rotation_.append("270", "Counter-clockwise");
  reflection_.append("0", "None");
  reflection_.append("X", "Horizontal");
  reflection_.append("Y", "Vertical");
  reflection_.append("XY", "Both axes");

  output_combo_.signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    selected_index_ = output_combo_.get_active_row_number();
    show_selected();
    redraw();
  });
  enabled_.property_active().signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    push_selected_from_fields();
  });
  primary_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    if (OutputConfig* output = selected()) {
      if (primary_.get_active()) {
        for (auto& other : outputs_) {
          other.primary = false;
        }
        output->primary = true;
      }
      mark_dirty();
      redraw();
    }
  });
  resolution_.signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    push_selected_from_fields();
  });
  refresh_.signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    push_selected_from_fields();
  });
  rotation_.signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    push_selected_from_fields();
  });
  reflection_.signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    push_selected_from_fields();
  });
  scale_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    push_selected_from_fields();
    sync_scale_buttons();
  });
  auto move_output = [this] {
    if (freeze_ > 0) {
      return;
    }
    if (OutputConfig* output = selected()) {
      output->x = x_pos_.get_value_as_int();
      output->y = y_pos_.get_value_as_int();
      mark_dirty();
      redraw();
    }
  };
  x_pos_.signal_value_changed().connect(move_output);
  y_pos_.signal_value_changed().connect(move_output);

  auto* policies = make_vbox(8);
  on_connect_.append("0", "Do nothing");
  on_connect_.append("1", "Show the display dialog");
  on_connect_.append("2", "Mirror displays");
  on_connect_.append("3", "Extend the desktop");
  auto_profile_.append("0", "Never");
  auto_profile_.append("1", "On connect");
  auto_profile_.append("2", "On disconnect");
  auto_profile_.append("3", "Always");
  FormGrid policy_form;
  policy_form.add("New display", on_connect_);
  policy_form.add("Saved profiles", auto_profile_);
  policies->pack_start(policy_form.grid(), Gtk::PACK_SHRINK);
  on_connect_.signal_changed().connect([this] {
    if (freeze_ > 0 || on_connect_.get_active_id().empty()) {
      return;
    }
    store_.set_int("displays", "/Notify", std::stoi(on_connect_.get_active_id()));
  });
  auto_profile_.signal_changed().connect([this] {
    if (freeze_ > 0 || auto_profile_.get_active_id().empty()) {
      return;
    }
    store_.set_int("displays", "/AutoEnableProfiles",
                   std::stoi(auto_profile_.get_active_id()));
  });
  right->pack_start(*make_frame("When displays are plugged in", *policies), Gtk::PACK_SHRINK);

  auto* profile_box = make_vbox(8);
  profile_box->pack_start(profiles_, Gtk::PACK_SHRINK);
  auto* profile_buttons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  auto* load = Gtk::manage(new Gtk::Button("Load"));
  auto* save = Gtk::manage(new Gtk::Button("Save…"));
  auto* remove = Gtk::manage(new Gtk::Button("Delete"));
  load->signal_clicked().connect([this] {
    if (!profiles_.get_active_id().empty()) {
      load_profile(profiles_.get_active_id());
    }
  });
  save->signal_clicked().connect(sigc::mem_fun(*this, &DisplayPage::save_profile));
  remove->signal_clicked().connect(sigc::mem_fun(*this, &DisplayPage::delete_profile));
  profile_buttons->pack_start(*load, Gtk::PACK_SHRINK);
  profile_buttons->pack_start(*save, Gtk::PACK_SHRINK);
  profile_buttons->pack_start(*remove, Gtk::PACK_SHRINK);
  profile_box->pack_start(*profile_buttons, Gtk::PACK_SHRINK);
  right->pack_start(*make_frame("Profiles", *profile_box), Gtk::PACK_SHRINK);

  status_ = make_label("");
  status_->set_line_wrap(true);
  right->pack_start(*status_, Gtk::PACK_SHRINK);

  load_hardware();
  signal_map().connect([this] {
    if (screen_watch_) {
      return;
    }
    randr_.select_screen_events();
    auto display = get_display();
    if (!display) {
      return;
    }
    auto root = display->get_default_screen()->get_root_window();
    if (!root) {
      return;
    }
    root_window_ = root->gobj();
    gdk_window_add_filter(root_window_, &DisplayPage::screen_filter, this);
    screen_watch_ = true;
  });
}

DisplayPage::~DisplayPage() {
  close_keep_preview();
  // The widget's display pointer is already gone while gtkmm destroys it.
  // Removing the filter through get_display() crashed on every OK, Cancel,
  // and window close. A queued scrollbar refresh must not run after that.
  alive_ = false;
  if (paint_idle_.connected()) {
    paint_idle_.disconnect();
  }
  if (screen_watch_ && root_window_ != nullptr && !gdk_window_is_destroyed(root_window_)) {
    gdk_window_remove_filter(root_window_, &DisplayPage::screen_filter, this);
  }
  screen_watch_ = false;
  root_window_ = nullptr;
}

GdkFilterReturn DisplayPage::screen_filter(GdkXEvent* xevent, GdkEvent*, gpointer data) {
  auto* self = static_cast<DisplayPage*>(data);
  // hardware_changed_ covers the gap after Apply and before the confirm
  // dialog. Reloading there would treat the rejected mode as the baseline.
  if (self == nullptr || !self->alive_ || self->applying_ || self->confirming_ || self->dirty_ ||
      self->hardware_changed_ || !self->randr_.is_screen_change(xevent)) {
    return GDK_FILTER_CONTINUE;
  }
  self->load_hardware();
  self->dirty_ = false;
  return GDK_FILTER_CONTINUE;
}

void DisplayPage::load_hardware() {
  auto next = randr_.read();
  if (next.empty() && !outputs_.empty()) {
    // A failed RandR change must not wipe the form the user is editing.
    status_->set_text(randr_.error().empty() ? "The display layout could not be read. The previous settings are still shown."
                                             : randr_.error());
    return;
  }
  const bool preserve_baseline = dirty_ || hardware_changed_ || confirming_ || !pending_revert_.empty();
  outputs_ = std::move(next);
  if (outputs_.empty()) {
    status_->set_text(randr_.error().empty()
                          ? "No connected displays were found."
                          : randr_.error());
  } else if (!preserve_baseline) {
    status_->set_text("Drag a display to move it. Mirror and Extend update the layout. Apply asks you to keep it.");
  }
  if (!preserve_baseline) {
    session_open_ = outputs_;
    confirmed_ = outputs_;
  }
  selected_index_ = 0;
  rebuild_output_combo();
  show_selected();
  {
    Freeze freeze(freeze_);
    on_connect_.set_active_id(std::to_string(store_.get_int("displays", "/Notify", 1)));
    auto_profile_.set_active_id(
        std::to_string(store_.get_int("displays", "/AutoEnableProfiles", 3)));
  }
  reload_profiles();
  redraw();
}

void DisplayPage::rebuild_output_combo() {
  Freeze freeze(freeze_);
  output_combo_.remove_all();
  for (const auto& output : outputs_) {
    const std::string label = output.friendly.empty() ? output.name : output.friendly;
    output_combo_.append(output.name, label + "  (" + output.name + ")");
  }
  if (!outputs_.empty()) {
    output_combo_.set_active(std::clamp(selected_index_, 0, static_cast<int>(outputs_.size()) - 1));
  }
}

OutputConfig* DisplayPage::selected() {
  if (selected_index_ < 0 || selected_index_ >= static_cast<int>(outputs_.size())) {
    return nullptr;
  }
  return &outputs_[static_cast<std::size_t>(selected_index_)];
}

void DisplayPage::show_selected() {
  Freeze freeze(freeze_);
  OutputConfig* output = selected();
  const bool have = output != nullptr;
  enabled_.set_sensitive(have);
  primary_.set_sensitive(have);
  resolution_.set_sensitive(have && output->active);
  refresh_.set_sensitive(have && output->active);
  rotation_.set_sensitive(have && output->active);
  reflection_.set_sensitive(have && output->active);
  scale_.set_sensitive(have && output->active);
  x_pos_.set_sensitive(have && output->active);
  y_pos_.set_sensitive(have && output->active);
  if (!have) {
    return;
  }
  enabled_.set_active(output->active);
  primary_.set_active(output->primary);
  resolution_.remove_all();
  std::set<std::string> seen;
  long long best_area = 0;
  for (const auto& mode : output->modes) {
    best_area = std::max(best_area, static_cast<long long>(mode.width) * mode.height);
  }
  auto describe = [&](int width, int height) {
    std::string text = std::to_string(width) + " × " + std::to_string(height);
    const std::string aspect = aspect_ratio_text(width, height);
    if (!aspect.empty()) {
      text += "    " + aspect;
    }
    if (best_area > 0 && static_cast<long long>(width) * height == best_area) {
      text += " · Recommended";
    }
    return text;
  };
  for (const auto& mode : output->modes) {
    const std::string id = format_resolution(mode.width, mode.height);
    if (seen.insert(id).second) {
      resolution_.append(id, describe(mode.width, mode.height));
    }
  }
  const std::string current = format_resolution(output->width, output->height);
  if (seen.count(current) == 0 && output->width > 0) {
    resolution_.append(current, describe(output->width, output->height));
  }
  resolution_.set_active_id(current);
  refresh_.remove_all();
  double best_rate = output->refresh > 0.5 ? output->refresh : 0;
  int refresh_rows = 0;
  for (const auto& mode : output->modes) {
    if (mode.width != output->width || mode.height != output->height || mode.refresh <= 0.5) {
      continue;
    }
    if (mode.refresh > best_rate && output->refresh <= 0.5) {
      best_rate = mode.refresh;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.2f", mode.refresh);
    refresh_.append(buffer, std::string(buffer) + " Hz");
    ++refresh_rows;
  }
  const bool known = refresh_rows > 0 || output->refresh > 0.5;
  refresh_.set_no_show_all(!known);
  refresh_.set_visible(known);
  if (refresh_label_ != nullptr) {
    refresh_label_->set_no_show_all(!known);
    refresh_label_->set_visible(known);
  }
  if (known) {
    char current_rate[32];
    const double shown = output->refresh > 0.5 ? output->refresh : best_rate;
    std::snprintf(current_rate, sizeof current_rate, "%.2f", shown);
    if (refresh_rows == 0) {
      refresh_.append(current_rate, std::string(current_rate) + " Hz");
    }
    refresh_.set_active_id(current_rate);
    if (refresh_.get_active_id().empty()) {
      refresh_.set_active(0);
    }
  }
  rotation_.set_active_id(std::to_string(output->rotation));
  if (rotation_.get_active_id().empty()) {
    rotation_.set_active_id("0");
  }
  reflection_.set_active_id(output->reflection.empty() ? "0" : output->reflection);
  scale_.set_value(output->scale > 0 ? output->scale : 1.0);
  x_pos_.set_value(output->x);
  y_pos_.set_value(output->y);
  sync_scale_buttons();
  refresh_output_heading();
}

void DisplayPage::push_selected_from_fields() {
  OutputConfig* output = selected();
  if (output == nullptr) {
    return;
  }
  const bool was_active = output->active;
  output->active = enabled_.get_active();
  if (output->active && !was_active) {
    int active = 0;
    for (const auto& other : outputs_) {
      if (other.active) {
        ++active;
      }
    }
    if (active == 1) {
      output->primary = true;
    }
  }
  if (!output->active) {
    int active = 0;
    for (const auto& other : outputs_) {
      if (&other != output && other.active) {
        ++active;
      }
    }
    if (active == 0) {
      Freeze freeze(freeze_);
      output->active = true;
      enabled_.set_active(true);
      status_->set_text("At least one display has to stay on.");
      return;
    }
    if (output->primary) {
      output->primary = false;
      for (auto& other : outputs_) {
        if (other.active) {
          other.primary = true;
          break;
        }
      }
    }
  }
  int width = output->width;
  int height = output->height;
  if (!resolution_.get_active_id().empty()) {
    parse_resolution(resolution_.get_active_id(), width, height);
  }
  const bool size_changed = width != output->width || height != output->height;
  output->width = width;
  output->height = height;
  if (size_changed) {
    output->refresh = closest_refresh(*output, width, height, output->refresh);
  } else if (!refresh_.get_active_id().empty()) {
    try {
      output->refresh = std::stod(refresh_.get_active_id());
    } catch (...) {
    }
  }
  if (!rotation_.get_active_id().empty()) {
    output->rotation = std::stoi(rotation_.get_active_id());
  }
  if (!reflection_.get_active_id().empty()) {
    output->reflection = reflection_.get_active_id();
  }
  output->scale = scale_.get_value();
  output->primary = primary_.get_active();
  if (output->primary) {
    for (auto& other : outputs_) {
      if (&other != output) {
        other.primary = false;
      }
    }
  }
  ensure_primary();
  mark_dirty();
  if (size_changed) {
    show_selected();
  }
  redraw();
}

void DisplayPage::ensure_primary() {
  bool any = false;
  OutputConfig* first = nullptr;
  for (auto& output : outputs_) {
    if (!output.active) {
      output.primary = false;
      continue;
    }
    if (first == nullptr) {
      first = &output;
    }
    if (output.primary) {
      if (any) {
        output.primary = false;
      }
      any = true;
    }
  }
  if (!any && first != nullptr) {
    first->primary = true;
  }
}

void DisplayPage::mark_dirty() {
  if (!dirty_) {
    dirty_ = true;
    pending_changed.emit();
  } else {
    dirty_ = true;
  }
}

void DisplayPage::on_page_shown() {
  if (scroll_ != nullptr && scroll_->get_vadjustment()) {
    scroll_->get_vadjustment()->set_value(0);
  }
  if (!paint_idle_.connected()) {
    paint_idle_ = Glib::signal_idle().connect([this]() {
      if (alive_) {
        paint_now();
      }
      return false;
    });
  }
}

void DisplayPage::paint_now() {
  if (scroll_ == nullptr || scroll_->get_child() == nullptr) {
    return;
  }
  auto* child = scroll_->get_child();
  if (!child->get_realized() || child->get_allocated_width() < 2 || child->get_allocated_height() < 2) {
    return;
  }
  // gtk_widget_draw while the child still needs an allocation paints a solid
  // rectangle over the form and leaves it blank. The scrollbar policy is the
  // only thing this refresh has to change.
  int minimum_height = 0;
  int natural_height = 0;
  child->get_preferred_height(minimum_height, natural_height);
  const bool overflow = natural_height > scroll_->get_allocated_height() + 8;
  scroll_->set_policy(Gtk::POLICY_NEVER, overflow ? Gtk::POLICY_ALWAYS : Gtk::POLICY_AUTOMATIC);
  if (overflow) {
    auto* bar = scroll_->get_vscrollbar();
    bar->set_size_request(14, -1);
    bar->show();
  }
}

void DisplayPage::redraw() {
  arrange_.queue_draw();
  sync_arrange_buttons();
  paint_now();
}

bool DisplayPage::on_arrange_draw(const Cairo::RefPtr<Cairo::Context>& context) {
  preview_width_ = std::max(1, arrange_.get_allocated_width());
  preview_height_ = std::max(1, arrange_.get_allocated_height());
  paint_preview(context);
  return true;
}

void DisplayPage::sync_scale_buttons() {
  const double value = scale_.get_value();
  const double presets[] = {1.0, 1.25, 1.5, 1.75, 2.0};
  bool matched = false;
  for (int i = 0; i < 5; ++i) {
    if (scale_buttons_[i] == nullptr) {
      continue;
    }
    const bool on = std::abs(value - presets[i]) < 0.011;
    matched = matched || on;
    if (on) {
      scale_buttons_[i]->get_style_context()->add_class("lcos-on");
    } else {
      scale_buttons_[i]->get_style_context()->remove_class("lcos-on");
    }
  }
  if (custom_scale_ != nullptr) {
    if (matched) {
      custom_scale_->get_style_context()->remove_class("lcos-on");
    } else {
      custom_scale_->get_style_context()->add_class("lcos-on");
    }
  }
}

void DisplayPage::sync_arrange_buttons() {
  if (extend_button_ == nullptr || mirror_button_ == nullptr) {
    return;
  }
  int active = 0;
  bool mirror = true;
  bool first = true;
  int x = 0;
  int y = 0;
  for (const auto& output : outputs_) {
    if (!output.active) {
      continue;
    }
    ++active;
    if (first) {
      x = output.x;
      y = output.y;
      first = false;
    } else if (output.x != x || output.y != y) {
      mirror = false;
    }
  }
  if (active < 2) {
    mirror = false;
  }
  if (mirror) {
    mirror_button_->get_style_context()->add_class("lcos-on");
    extend_button_->get_style_context()->remove_class("lcos-on");
  } else {
    extend_button_->get_style_context()->add_class("lcos-on");
    mirror_button_->get_style_context()->remove_class("lcos-on");
  }
}

void DisplayPage::refresh_output_heading() {
  if (output_heading_ == nullptr) {
    return;
  }
  const OutputConfig* output = selected();
  if (output == nullptr) {
    output_heading_->set_text("Display");
    return;
  }
  const std::string title = output_ui_name(*output);
  std::string text = title.empty() || title == output->name ? output->name
                                                           : title + " — " + output->name;
  if (output->mm_width > 0 && output->mm_height > 0) {
    const double inches = std::sqrt(static_cast<double>(output->mm_width) * output->mm_width +
                                    static_cast<double>(output->mm_height) * output->mm_height) /
                          25.4;
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, ", %.1f″", inches);
    text += buffer;
  }
  output_heading_->set_text(text);
}

const OutputConfig* DisplayPage::selected() const {
  if (selected_index_ < 0 || selected_index_ >= static_cast<int>(outputs_.size())) {
    return nullptr;
  }
  return &outputs_[static_cast<std::size_t>(selected_index_)];
}

void DisplayPage::paint_preview(const Cairo::RefPtr<Cairo::Context>& context) {
  const int width = std::max(1, preview_width_);
  const int height = std::max(1, preview_height_);
  context->save();
  context->rectangle(0, 0, width, height);
  context->clip();
  auto backdrop = Cairo::LinearGradient::create(0, 0, 0, height);
  backdrop->add_color_stop_rgb(0, 0.863, 0.847, 0.824);
  backdrop->add_color_stop_rgb(1, 0.831, 0.812, 0.784);
  context->set_source(backdrop);
  context->paint();
  context->set_source_rgba(0, 0, 0, 0.09);
  for (int y = 8; y < height; y += 16) {
    for (int x = 8; x < width; x += 16) {
      context->arc(x, y, 1.0, 0, 6.28318530718);
      context->fill();
    }
  }
  int max_x = 1;
  int max_y = 1;
  for (const auto& output : outputs_) {
    max_x = std::max(max_x, std::max(0, output.x) + footprint_width(output));
    max_y = std::max(max_y, std::max(0, output.y) + footprint_height(output));
  }
  const double margin = 16;
  view_scale_ = std::min((width - 2 * margin) / max_x, (height - 2 * margin) / max_y);
  if (view_scale_ <= 0) {
    view_scale_ = 1;
  }
  origin_x_ = (width - max_x * view_scale_) / 2.0;
  origin_y_ = (height - max_y * view_scale_) / 2.0;
  for (int i = 0; i < static_cast<int>(outputs_.size()); ++i) {
    const OutputConfig& output = outputs_[static_cast<std::size_t>(i)];
    const double x = origin_x_ + output.x * view_scale_;
    const double y = origin_y_ + output.y * view_scale_;
    const double box_w = std::max(48.0, footprint_width(output) * view_scale_);
    const double box_h = std::max(36.0, footprint_height(output) * view_scale_);
    context->set_source_rgba(0.12, 0.12, 0.16, 0.28);
    rounded_rect(context, x + 3, y + 6, box_w, box_h, 7);
    context->fill();
    auto bezel = Cairo::LinearGradient::create(x, y, x, y + box_h);
    bezel->add_color_stop_rgb(0, 0.29, 0.31, 0.34);
    bezel->add_color_stop_rgb(1, 0.20, 0.22, 0.24);
    context->set_source(bezel);
    rounded_rect(context, x, y, box_w, box_h, 7);
    context->fill();
    const double inset = 5;
    auto screen = Cairo::LinearGradient::create(x, y, x + box_w, y + box_h);
    if (!output.active) {
      screen->add_color_stop_rgb(0, 0.55, 0.56, 0.58);
      screen->add_color_stop_rgb(1, 0.42, 0.43, 0.45);
    } else if (i == selected_index_) {
      screen->add_color_stop_rgb(0, 0.663, 0.776, 0.918);
      screen->add_color_stop_rgb(1, 0.435, 0.592, 0.796);
    } else {
      screen->add_color_stop_rgb(0, 0.663, 0.710, 0.769);
      screen->add_color_stop_rgb(1, 0.498, 0.561, 0.647);
    }
    context->set_source(screen);
    rounded_rect(context, x + inset, y + inset, std::max(1.0, box_w - 2 * inset),
                 std::max(1.0, box_h - 2 * inset), 3);
    context->fill();
    if (i == selected_index_) {
      context->set_source_rgb(1, 1, 1);
      context->set_line_width(2);
      rounded_rect(context, x - 2, y - 2, box_w + 4, box_h + 4, 8);
      context->stroke();
      context->set_source_rgb(0.435, 0.592, 0.788);
      context->set_line_width(2);
      rounded_rect(context, x - 4, y - 4, box_w + 8, box_h + 8, 9);
      context->stroke();
    }
    context->set_source_rgb(1, 1, 1);
    context->arc(x + inset + 16, y + inset + 16, 10, 0, 6.28318530718);
    context->set_source_rgba(1, 1, 1, 0.92);
    context->fill();
    context->set_source_rgb(0.184, 0.333, 0.498);
    context->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
    context->set_font_size(11);
    const std::string number = std::to_string(i + 1);
    context->move_to(x + inset + 12, y + inset + 20);
    context->show_text(number);
    context->set_source_rgb(1, 1, 1);
    context->set_font_size(13);
    context->move_to(x + inset + 30, y + inset + 20);
    context->show_text(output_ui_name(output));
    context->set_font_size(11);
    context->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
    context->move_to(x + inset + 10, y + inset + 38);
    context->show_text(output.active ? output.name + " · " + std::to_string(output.width) + " × " +
                                           std::to_string(output.height)
                                     : std::string("Off"));
    if (output.primary && output.active && box_h > 70) {
      const std::string chip = "Primary";
      context->set_font_size(10);
      Cairo::TextExtents chip_extents;
      context->get_text_extents(chip, chip_extents);
      const double chip_w = chip_extents.width + 14;
      const double chip_h = 16;
      const double chip_x = x + inset + 8;
      const double chip_y = y + box_h - inset - chip_h - 4;
      context->set_source_rgba(1, 1, 1, 0.25);
      rounded_rect(context, chip_x, chip_y, chip_w, chip_h, 8);
      context->fill();
      context->set_source_rgb(1, 1, 1);
      context->move_to(chip_x + 7, chip_y + 12);
      context->show_text(chip);
    }
  }
  if (outputs_.empty()) {
    context->set_source_rgb(0.2, 0.2, 0.2);
    context->move_to(24, height / 2.0);
    context->show_text("No displays");
  }
  context->restore();
}

int DisplayPage::hit_test(double x, double y) const {
  for (int i = static_cast<int>(outputs_.size()) - 1; i >= 0; --i) {
    const OutputConfig& output = outputs_[static_cast<std::size_t>(i)];
    const double left = origin_x_ + output.x * view_scale_;
    const double top = origin_y_ + output.y * view_scale_;
    const double box_w = std::max(48.0, footprint_width(output) * view_scale_);
    const double box_h = std::max(36.0, footprint_height(output) * view_scale_);
    if (x >= left && x <= left + box_w && y >= top && y <= top + box_h) {
      return i;
    }
  }
  return -1;
}

bool DisplayPage::on_button_press(GdkEventButton* event) {
  if (event->button != 1) {
    return false;
  }
  const int index = hit_test(event->x, event->y);
  if (index < 0) {
    return false;
  }
  selected_index_ = index;
  output_combo_.set_active(index);
  dragging_ = true;
  const OutputConfig& output = outputs_[static_cast<std::size_t>(index)];
  const int world_x = static_cast<int>(std::lround((event->x - origin_x_) / view_scale_));
  const int world_y = static_cast<int>(std::lround((event->y - origin_y_) / view_scale_));
  grab_dx_ = world_x - output.x;
  grab_dy_ = world_y - output.y;
  redraw();
  return true;
}

bool DisplayPage::on_motion(GdkEventMotion* event) {
  if (!dragging_ || selected() == nullptr || view_scale_ <= 0) {
    return false;
  }
  OutputConfig& output = *selected();
  output.x = static_cast<int>(std::lround((event->x - origin_x_) / view_scale_)) - grab_dx_;
  output.y = static_cast<int>(std::lround((event->y - origin_y_) / view_scale_)) - grab_dy_;
  const int tolerance = std::max(8, static_cast<int>(std::lround(14.0 / view_scale_)));
  const int own_w = footprint_width(output);
  const int own_h = footprint_height(output);
  if (std::abs(output.x) <= tolerance) {
    output.x = 0;
  }
  if (std::abs(output.y) <= tolerance) {
    output.y = 0;
  }
  for (int i = 0; i < static_cast<int>(outputs_.size()); ++i) {
    if (i == selected_index_ || !outputs_[static_cast<std::size_t>(i)].active) {
      continue;
    }
    const OutputConfig& other = outputs_[static_cast<std::size_t>(i)];
    const int other_w = footprint_width(other);
    const int other_h = footprint_height(other);
    if (std::abs(output.x - (other.x + other_w)) <= tolerance) {
      output.x = other.x + other_w;
    }
    if (std::abs((output.x + own_w) - other.x) <= tolerance) {
      output.x = other.x - own_w;
    }
    if (std::abs(output.y - other.y) <= tolerance) {
      output.y = other.y;
    }
    if (std::abs(output.y - (other.y + other_h)) <= tolerance) {
      output.y = other.y + other_h;
    }
    if (std::abs((output.y + own_h) - other.y) <= tolerance) {
      output.y = other.y - own_h;
    }
  }
  {
    Freeze freeze(freeze_);
    x_pos_.set_value(output.x);
    y_pos_.set_value(output.y);
  }
  mark_dirty();
  redraw();
  return true;
}

bool DisplayPage::on_button_release(GdkEventButton*) {
  dragging_ = false;
  return false;
}

void DisplayPage::write_scheme(const std::string& scheme, bool track) {
  const bool previous = store_.tracking();
  if (!track) {
    store_.set_tracking(false);
  }
  for (const auto& output : outputs_) {
    const auto path = [&](const std::string& leaf) {
      return display_prop(scheme, output.name, leaf);
    };
    store_.set_string("displays", path(""), output.friendly.empty() ? output.name : output.friendly);
    store_.set_bool("displays", path("Active"), output.active);
    if (!output.edid.empty()) {
      store_.set_string("displays", path("EDID"), output.edid);
    }
    if (!output.active) {
      continue;
    }
    store_.set_string("displays", path("Resolution"),
                      format_resolution(output.width, output.height));
    store_.set_double("displays", path("RefreshRate"), output.refresh);
    store_.set_int("displays", path("Rotation"), output.rotation);
    store_.set_string("displays", path("Reflection"),
                      output.reflection.empty() ? "0" : output.reflection);
    store_.set_bool("displays", path("Primary"), output.primary);
    // xfsettingsd applies this number as the RandR transform. UI scale 2
    // has to be stored as 0.5 or the framebuffer doubles and the UI shrinks.
    const double scalex = xfconf_scale_from_ui(output.scale);
    store_.set_double("displays", path("Scale"), scalex);
    // 4.20 reads /Scale. 4.18 reads /Scale/X and /Scale/Y, and 4.20 still
    // prefers those when they exist, so both forms stay in agreement.
    store_.set_double("displays", path("Scale/X"), scalex);
    store_.set_double("displays", path("Scale/Y"), scalex);
    store_.set_int("displays", path("Position/X"), output.x);
    store_.set_int("displays", path("Position/Y"), output.y);
  }
  if (!track) {
    store_.set_tracking(previous);
  }
}

bool DisplayPage::commit_pending() {
  if (!dirty_) {
    return true;
  }
  if (outputs_.empty()) {
    Gtk::MessageDialog dialog(parent_, "There is no display to change.", false,
                              Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK, true);
    dialog.run();
    return false;
  }
  ensure_primary();
  normalize_positions(outputs_);
  show_selected();
  redraw();
  // Read the live layout now. confirmed_ is not used: a screen event can
  // replace it with the mode we are about to try.
  std::vector<OutputConfig> revert_to = randr_.read();
  if (revert_to.empty()) {
    revert_to = confirmed_.empty() ? session_open_ : confirmed_;
  }
  pending_revert_ = revert_to;
  applying_ = true;
  const std::string error = randr_.apply(outputs_);
  applying_ = false;
  if (!error.empty()) {
    pending_revert_.clear();
    clear_temp_scheme(false);
    Gtk::MessageDialog dialog(parent_, "Could not apply the display layout.", false,
                              Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
    dialog.set_secondary_text(error);
    dialog.run();
    show_selected();
    redraw();
    return false;
  }
  hardware_changed_ = true;
  write_scheme("Temp", false);
  store_.set_string_untracked("displays", "/Schemes/Apply", "Temp");

  confirming_ = true;
  ConfirmDialog dialog(parent_, outputs_);
  const int response = dialog.run();
  confirming_ = false;
  if (response == Gtk::RESPONSE_ACCEPT) {
    // Write the kept layout before asking xfsettingsd to apply Default.
    // Clearing Temp first would make it reapply the previous profile.
    write_scheme("Default", true);
    store_.reset_untracked("displays", "/Temp", true);
    store_.set_string("displays", "/ActiveProfile", "Default");
    store_.set_string_untracked("displays", "/Schemes/Apply", "Default");
    confirmed_ = outputs_;
    session_open_ = outputs_;
    hardware_changed_ = false;
    pending_revert_.clear();
    // Keep confirmed the layout. A later Cancel must not put the old one back.
    store_.drop_channel("displays");
    dirty_ = false;
    pending_changed.emit();
    status_->set_text("Display layout kept.");
    reload_profiles();
    return true;
  }
  // Drop Temp without asking xfsettingsd to apply Default. A profile with no
  // Default scheme has nothing for it to restore, and that request used to
  // leave the rejected mode on screen while this window said it had reverted.
  clear_temp_scheme(false);
  std::string revert_error;
  if (!apply_saved_layout(revert_to, revert_error)) {
    const std::string named =
        revert_error.empty() ? "Could not restore " + layout_names(revert_to) + "." : revert_error;
    status_->set_text(named);
    Gtk::MessageDialog failed(parent_, "The previous display layout was not restored.", false,
                              Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
    failed.set_secondary_text(named);
    failed.run();
    show_selected();
    redraw();
    return false;
  }
  outputs_ = confirmed_;
  session_open_ = confirmed_;
  hardware_changed_ = false;
  pending_revert_.clear();
  dirty_ = false;
  pending_changed.emit();
  selected_index_ = outputs_.empty()
                        ? 0
                        : std::clamp(selected_index_, 0, static_cast<int>(outputs_.size()) - 1);
  rebuild_output_combo();
  status_->set_text("Reverted to the previous layout. Adjust it and press Apply to try again.");
  show_selected();
  redraw();
  return false;
}

void DisplayPage::clear_temp_scheme(bool trigger_apply) {
  store_.reset_untracked("displays", "/Temp", true);
  if (store_.get_string("displays", "/ActiveProfile", "") == "Temp") {
    store_.set_string_untracked("displays", "/ActiveProfile", "Default");
  }
  if (trigger_apply) {
    store_.set_string_untracked("displays", "/Schemes/Apply", "Default");
  }
}

bool DisplayPage::apply_saved_layout(const std::vector<OutputConfig>& layout, std::string& error) {
  if (layout.empty()) {
    error = "Could not restore the display layout: there is no saved layout.";
    return false;
  }
  applying_ = true;
  error = randr_.apply(layout);
  applying_ = false;
  if (!error.empty()) {
    error = "Could not restore " + layout_names(layout) + ". " + error;
    return false;
  }
  const auto now = randr_.read();
  if (!layouts_equivalent(now, layout)) {
    error = "Could not restore " + layout_names(layout) +
            ". The screen is still using a different layout.";
    return false;
  }
  confirmed_ = now.empty() ? layout : now;
  return true;
}

std::string DisplayPage::restore_session_hardware() {
  if (!hardware_changed_) {
    return {};
  }
  const std::vector<OutputConfig> target = !pending_revert_.empty() ? pending_revert_ : session_open_;
  // RandR is last. /Schemes/Apply is not set here; a missing Default scheme
  // must not run after the restore and put the rejected mode back.
  clear_temp_scheme(false);
  std::string error;
  if (!apply_saved_layout(target, error)) {
    return error.empty() ? "Could not restore " + layout_names(target) + "." : error;
  }
  outputs_ = confirmed_;
  session_open_ = confirmed_;
  hardware_changed_ = false;
  pending_revert_.clear();
  rebuild_output_combo();
  show_selected();
  redraw();
  return {};
}

std::string DisplayPage::pending_detail() const {
  if (!dirty_) {
    return {};
  }
  std::string text;
  for (const auto& output : outputs_) {
    const OutputConfig* was = nullptr;
    for (const auto& old : confirmed_) {
      if (old.name == output.name) {
        was = &old;
        break;
      }
    }
    const bool changed =
        was == nullptr || output.active != was->active || output.width != was->width ||
        output.height != was->height || !refresh_matches(output.refresh, was->refresh) ||
        std::abs(output.scale - was->scale) > 0.01 || output.rotation != was->rotation ||
        output.reflection != was->reflection || output.primary != was->primary ||
        output.x != was->x || output.y != was->y;
    if (!changed) {
      continue;
    }
    if (!text.empty()) {
      text += "; ";
    }
    if (was == nullptr) {
      text += output_summary_line(output);
    } else {
      text += output_ui_name(output) + ": " + output_summary_line(*was) + " → " +
              output_summary_line(output);
    }
  }
  if (text.empty()) {
    return "Display layout has not been kept.";
  }
  return text;
}

bool DisplayPage::discard_pending() {
  if (!dirty_ || confirming_ || applying_) {
    return false;
  }
  outputs_ = confirmed_.empty() ? session_open_ : confirmed_;
  dirty_ = false;
  pending_changed.emit();
  selected_index_ = outputs_.empty()
                        ? 0
                        : std::clamp(selected_index_, 0, static_cast<int>(outputs_.size()) - 1);
  rebuild_output_combo();
  show_selected();
  redraw();
  return true;
}

void DisplayPage::open_keep_preview() {
  close_keep_preview();
  auto* dialog = new ConfirmDialog(parent_, outputs_);
  dialog->set_modal(false);
  keep_preview_ = dialog;
  dialog->signal_response().connect([this](int) { close_keep_preview(); });
  dialog->show_all();
  dialog->present();
}

void DisplayPage::close_keep_preview() {
  if (keep_preview_ != nullptr) {
    Gtk::Window* dialog = keep_preview_;
    keep_preview_ = nullptr;
    dialog->hide();
    delete dialog;
  }
}

namespace {

std::vector<std::pair<std::string, std::string>> display_strings(XfconfStore& store) {
  std::vector<std::pair<std::string, std::string>> props;
  for (const auto& entry : store.list("displays", "/")) {
    props.emplace_back(entry.first, entry.second.type == PropType::String ? entry.second.s : std::string());
  }
  return props;
}

}  // namespace

void DisplayPage::reload_profiles() {
  Freeze freeze(freeze_);
  const std::string previous = profiles_.get_active_id();
  profiles_.remove_all();
  const auto props = display_strings(store_);
  std::vector<std::string> edids;
  for (const auto& output : outputs_) {
    if (output.active && !output.edid.empty()) {
      edids.push_back(output.edid);
    }
  }
  std::set<std::string> ids;
  ids.insert("Default");
  if (edids.empty()) {
    std::set<std::string> seen;
    for (const auto& entry : props) {
      const auto slash = entry.first.find('/', 1);
      const std::string id = entry.first.size() > 1
                                 ? entry.first.substr(1, slash == std::string::npos ? std::string::npos : slash - 1)
                                 : std::string();
      if (id.empty() || !seen.insert(id).second || id == "Temp") {
        continue;
      }
      if (is_user_display_profile(id, props)) {
        ids.insert(id);
      }
    }
  } else {
    for (const auto& id : matching_display_profiles(edids, props)) {
      if (id != "Temp") {
        ids.insert(id);
      }
    }
  }
  for (const auto& id : ids) {
    std::string label = id;
    if (id != "Default") {
      const PropValue value = store_.get("displays", "/" + id);
      if (value.type == PropType::String && !value.s.empty() && value.s != id) {
        label = value.s;
      }
    } else {
      label = "Default";
    }
    profiles_.append(id, label);
  }
  const std::string active = store_.get_string("displays", "/ActiveProfile", "Default");
  if (!previous.empty()) {
    profiles_.set_active_id(previous);
  }
  if (profiles_.get_active_id().empty()) {
    profiles_.set_active_id(active);
  }
  if (profiles_.get_active_id().empty()) {
    profiles_.set_active(0);
  }
}

void DisplayPage::load_profile(const std::string& id) {
  bool any = false;
  for (auto& output : outputs_) {
    const std::string active_path = display_prop(id, output.name, "Active");
    if (!store_.has("displays", active_path)) {
      continue;
    }
    any = true;
    output.active = store_.get_bool("displays", active_path, output.active);
    const std::string resolution =
        store_.get_string("displays", display_prop(id, output.name, "Resolution"), "");
    int width = output.width;
    int height = output.height;
    if (parse_resolution(resolution, width, height)) {
      output.width = width;
      output.height = height;
    }
    output.refresh = store_.get_double("displays", display_prop(id, output.name, "RefreshRate"),
                                       output.refresh);
    output.rotation =
        store_.get_int("displays", display_prop(id, output.name, "Rotation"), output.rotation);
    output.reflection = store_.get_string(
        "displays", display_prop(id, output.name, "Reflection"), output.reflection);
    double scale = store_.get_double("displays", display_prop(id, output.name, "Scale"), -1);
    if (scale < 0) {
      scale = store_.get_double("displays", display_prop(id, output.name, "Scale/X"), output.scale);
    }
    if (scale > 0) {
      output.scale = ui_scale_from_xfconf(scale);
    }
    output.x = store_.get_int("displays", display_prop(id, output.name, "Position/X"), output.x);
    output.y = store_.get_int("displays", display_prop(id, output.name, "Position/Y"), output.y);
    output.primary =
        store_.get_bool("displays", display_prop(id, output.name, "Primary"), false);
  }
  if (!any && id != "Default") {
    status_->set_text("That profile has no settings for the connected displays.");
    return;
  }
  ensure_primary();
  mark_dirty();
  show_selected();
  redraw();
  status_->set_text("Profile loaded into the layout. Press Apply to put it on screen.");
}

void DisplayPage::save_profile() {
  if (outputs_.empty()) {
    return;
  }
  Gtk::Dialog dialog("Save display profile", parent_, true);
  auto* entry = Gtk::manage(new Gtk::Entry());
  entry->set_placeholder_text("Profile name");
  entry->set_margin_top(8);
  entry->set_margin_bottom(8);
  entry->set_margin_start(8);
  entry->set_margin_end(8);
  dialog.get_content_area()->pack_start(*entry, Gtk::PACK_SHRINK);
  dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("Save", Gtk::RESPONSE_OK);
  dialog.show_all();
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  const std::string label = entry->get_text();
  if (trim_copy(label).empty()) {
    return;
  }
  const std::string id = profile_id_from_label(label);
  write_scheme(id, true);
  store_.set_string("displays", "/" + id, trim_copy(label));
  reload_profiles();
  profiles_.set_active_id(id);
  status_->set_text("Saved profile \"" + trim_copy(label) + "\".");
}

void DisplayPage::delete_profile() {
  const std::string id = profiles_.get_active_id();
  const auto props = display_strings(store_);
  if (id.empty() || id == "Default" || id == "Temp" || !is_user_display_profile(id, props)) {
    status_->set_text("The Default profile stays. Choose another profile to delete.");
    return;
  }
  store_.reset("displays", "/" + id, true);
  reload_profiles();
}

void DisplayPage::identify() {
  for (const auto& output : outputs_) {
    if (!output.active) {
      continue;
    }
    auto* popup = new Gtk::Window(Gtk::WINDOW_TOPLEVEL);
    popup->set_decorated(false);
    popup->set_keep_above(true);
    popup->set_skip_taskbar_hint(true);
    popup->set_skip_pager_hint(true);
    popup->set_default_size(220, 72);
    auto* label = Gtk::manage(new Gtk::Label(output.name + "\n" +
                                             format_resolution(output.width, output.height)));
    label->set_margin_top(12);
    label->set_margin_bottom(12);
    label->set_margin_start(16);
    label->set_margin_end(16);
    popup->add(*label);
    popup->move(std::max(0, output.x + 32), std::max(0, output.y + 32));
    popup->show_all();
    Glib::signal_timeout().connect_seconds(
        [popup]() {
          delete popup;
          return false;
        },
        3);
  }
}

}  // namespace lcos
