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
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace lcos {
namespace {

class ConfirmDialog : public Gtk::Dialog {
 public:
  explicit ConfirmDialog(Gtk::Window& parent)
      : Gtk::Dialog("Keep this display configuration?", parent, true) {
    set_default_size(420, 140);
    label_.set_margin_top(12);
    label_.set_margin_bottom(12);
    label_.set_margin_start(12);
    label_.set_margin_end(12);
    label_.set_line_wrap(true);
    get_content_area()->pack_start(label_, Gtk::PACK_EXPAND_WIDGET);
    add_button("Revert", Gtk::RESPONSE_REJECT);
    add_button("Keep", Gtk::RESPONSE_ACCEPT);
    set_keep_above(true);
    show_all();
    present();
    refresh_label();
    timer_ = Glib::signal_timeout().connect_seconds(
        sigc::mem_fun(*this, &ConfirmDialog::tick), 1);
  }

  ~ConfirmDialog() override {
    if (timer_.connected()) {
      timer_.disconnect();
    }
  }

 private:
  bool tick() {
    countdown_.tick();
    if (countdown_.decided) {
      response(countdown_.keep ? Gtk::RESPONSE_ACCEPT : Gtk::RESPONSE_REJECT);
      return false;
    }
    refresh_label();
    return true;
  }

  void refresh_label() {
    label_.set_text("The new layout is on screen. It will revert in " +
                    std::to_string(countdown_.seconds) +
                    " seconds unless you keep it.");
  }

  ConfirmCountdown countdown_;
  Gtk::Label label_;
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
  auto* right = make_vbox(10);
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

  FormGrid form;
  form.add("Display", output_combo_);
  enabled_.set_label("Use this display");
  primary_.set_label("Primary display");
  form.add_control(enabled_);
  form.add_control(primary_);
  form.add("Resolution", resolution_);
  form.add("Refresh rate", refresh_);
  form.add("Rotation", rotation_);
  form.add("Reflection", reflection_);
  scale_.set_range(0.1, 10.0);
  scale_.set_increments(0.1, 1.0);
  scale_.set_digits(2);
  scale_.set_value(1.0);
  form.add("Scale", scale_);
  x_pos_.set_range(-8000, 16000);
  y_pos_.set_range(-8000, 16000);
  x_pos_.set_increments(1, 50);
  y_pos_.set_increments(1, 50);
  x_pos_.set_digits(0);
  y_pos_.set_digits(0);
  form.add("Position X", x_pos_);
  form.add("Position Y", y_pos_);
  right->pack_start(form.grid(), Gtk::PACK_SHRINK);

  auto* arrange = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  arrange->set_margin_top(4);
  auto* mirror = Gtk::manage(new Gtk::Button("Mirror"));
  auto* extend = Gtk::manage(new Gtk::Button("Extend"));
  auto* identify = Gtk::manage(new Gtk::Button("Identify"));
  mirror->signal_clicked().connect([this] {
    arrange_mirror(outputs_);
    ensure_primary();
    mark_dirty();
    show_selected();
    redraw();
  });
  extend->signal_clicked().connect([this] {
    arrange_extend(outputs_);
    mark_dirty();
    show_selected();
    redraw();
  });
  identify->signal_clicked().connect(sigc::mem_fun(*this, &DisplayPage::identify));
  arrange->pack_start(*mirror, Gtk::PACK_SHRINK);
  arrange->pack_start(*extend, Gtk::PACK_SHRINK);
  arrange->pack_start(*identify, Gtk::PACK_SHRINK);
  right->pack_start(*arrange, Gtk::PACK_SHRINK);

  preview_.set_halign(Gtk::ALIGN_CENTER);
  right->pack_start(preview_, Gtk::PACK_SHRINK);
  preview_.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
                      Gdk::POINTER_MOTION_MASK | Gdk::BUTTON_MOTION_MASK);
  preview_.signal_button_press_event().connect(
      sigc::mem_fun(*this, &DisplayPage::on_button_press));
  preview_.signal_motion_notify_event().connect(sigc::mem_fun(*this, &DisplayPage::on_motion));
  preview_.signal_button_release_event().connect(
      sigc::mem_fun(*this, &DisplayPage::on_button_release));

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
  enabled_.signal_toggled().connect([this] {
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
  if (self == nullptr || !self->alive_ || self->applying_ || self->confirming_ || self->dirty_ ||
      !self->randr_.is_screen_change(xevent)) {
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
  outputs_ = std::move(next);
  if (outputs_.empty()) {
    status_->set_text(randr_.error().empty()
                          ? "No connected displays were found."
                          : randr_.error());
  } else {
    status_->set_text("Drag a display to move it. Mirror and Extend update the layout. Apply asks you to keep it.");
  }
  session_open_ = outputs_;
  confirmed_ = outputs_;
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
  for (const auto& mode : output->modes) {
    const std::string label = format_resolution(mode.width, mode.height);
    if (seen.insert(label).second) {
      resolution_.append(label, label);
    }
  }
  const std::string current = format_resolution(output->width, output->height);
  if (seen.count(current) == 0 && output->width > 0) {
    resolution_.append(current, current);
  }
  resolution_.set_active_id(current);
  refresh_.remove_all();
  int refresh_rows = 0;
  for (const auto& mode : output->modes) {
    if (mode.width != output->width || mode.height != output->height) {
      continue;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.2f", mode.refresh);
    refresh_.append(buffer, std::string(buffer) + " Hz");
    ++refresh_rows;
  }
  char current_rate[32];
  std::snprintf(current_rate, sizeof current_rate, "%.2f", output->refresh);
  if (refresh_rows == 0) {
    refresh_.append(current_rate, std::string(current_rate) + " Hz");
  }
  refresh_.set_active_id(current_rate);
  if (refresh_.get_active_id().empty()) {
    refresh_.set_active(0);
  }
  rotation_.set_active_id(std::to_string(output->rotation));
  if (rotation_.get_active_id().empty()) {
    rotation_.set_active_id("0");
  }
  reflection_.set_active_id(output->reflection.empty() ? "0" : output->reflection);
  scale_.set_value(output->scale > 0 ? output->scale : 1.0);
  x_pos_.set_value(output->x);
  y_pos_.set_value(output->y);
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
  scroll_->set_policy(Gtk::POLICY_AUTOMATIC,
                      overflow ? Gtk::POLICY_ALWAYS : Gtk::POLICY_AUTOMATIC);
  if (overflow) {
    auto* bar = scroll_->get_vscrollbar();
    bar->set_size_request(14, -1);
    bar->show();
  }
}

void DisplayPage::redraw() {
  const int allocated = get_allocated_width();
  preview_width_ = allocated > 80 ? allocated - 48 : 720;
  preview_height_ = 130;
  auto surface =
      Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, preview_width_, preview_height_);
  auto context = Cairo::Context::create(surface);
  paint_preview(context);
  preview_.set(Gdk::Pixbuf::create(surface, 0, 0, preview_width_, preview_height_));
  paint_now();
}

void DisplayPage::paint_preview(const Cairo::RefPtr<Cairo::Context>& context) {
  const int width = std::max(1, preview_width_);
  const int height = std::max(1, preview_height_);
  context->save();
  context->rectangle(0, 0, width, height);
  context->clip();
  context->set_source_rgb(0.91, 0.88, 0.82);
  context->paint();
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
    if (!output.active) {
      context->set_source_rgb(0.55, 0.53, 0.49);
    } else if (i == selected_index_) {
      context->set_source_rgb(0.77, 0.46, 0.16);
    } else {
      context->set_source_rgb(0.12, 0.23, 0.37);
    }
    rounded_rect(context, x, y, box_w, box_h, 8);
    context->fill();
    context->set_source_rgb(1, 1, 1);
    context->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
    context->set_font_size(13);
    context->move_to(x + 10, y + 22);
    context->show_text(output.friendly.empty() ? output.name : output.friendly);
    context->set_font_size(11);
    context->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
    context->move_to(x + 10, y + 40);
    context->show_text(output.active ? format_resolution(output.width, output.height)
                                     : std::string("Off"));
    if (output.primary && output.active) {
      context->move_to(x + 10, y + box_h - 12);
      context->show_text("Primary");
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
  applying_ = true;
  const std::string error = randr_.apply(outputs_);
  applying_ = false;
  if (!error.empty()) {
    clear_temp_scheme();
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
  ConfirmDialog dialog(parent_);
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
    // Keep confirmed the layout. A later Cancel must not put the old one back.
    store_.drop_channel("displays");
    dirty_ = false;
    pending_changed.emit();
    status_->set_text("Display layout kept.");
    reload_profiles();
    return true;
  }
  if (!confirmed_.empty()) {
    applying_ = true;
    const std::string revert_error = randr_.apply(confirmed_);
    applying_ = false;
    if (!revert_error.empty()) {
      clear_temp_scheme();
      status_->set_text(revert_error);
      show_selected();
      redraw();
      return false;
    }
  }
  clear_temp_scheme();
  status_->set_text("Reverted to the previous layout. Adjust it and press Apply to try again.");
  show_selected();
  redraw();
  return false;
}

void DisplayPage::clear_temp_scheme() {
  store_.reset_untracked("displays", "/Temp", true);
  if (store_.get_string("displays", "/ActiveProfile", "") == "Temp") {
    store_.set_string_untracked("displays", "/ActiveProfile", "Default");
  }
  store_.set_string_untracked("displays", "/Schemes/Apply", "Default");
}

void DisplayPage::restore_session_hardware() {
  if (!hardware_changed_) {
    return;
  }
  if (!session_open_.empty()) {
    randr_.apply(session_open_);
  }
  clear_temp_scheme();
  hardware_changed_ = false;
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
