// SPDX-License-Identifier: GPL-3.0-or-later

#include "widgets/drawn_controls.hpp"

#include <gtk/gtk.h>

#include <gdkmm/general.h>
#include <glibmm/main.h>
#include <atkmm/object.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/window.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stackswitcher.h>
#include <gtkmm/switch.h>
#include <gtkmm/treeview.h>
#include <gtkmm/viewport.h>

#include <algorithm>
#include <cmath>

namespace lcos {
namespace {

constexpr double kPi = 3.141592653589793;

struct SwitchPaint {
  double shown = 0;
  guint timer = 0;
};

void free_switch_paint(gpointer data) {
  auto* paint = static_cast<SwitchPaint*>(data);
  if (paint != nullptr && paint->timer != 0) {
    g_source_remove(paint->timer);
  }
  delete paint;
}

void rounded_rect(const Cairo::RefPtr<Cairo::Context>& cr, double x, double y, double w, double h,
                  double radius) {
  radius = std::min(radius, std::min(w, h) / 2.0);
  cr->begin_new_sub_path();
  cr->arc(x + w - radius, y + radius, radius, -kPi / 2.0, 0);
  cr->arc(x + w - radius, y + h - radius, radius, 0, kPi / 2.0);
  cr->arc(x + radius, y + h - radius, radius, kPi / 2.0, kPi);
  cr->arc(x + radius, y + radius, radius, kPi, 3.0 * kPi / 2.0);
  cr->close_path();
}

bool widget_focused(Gtk::Widget& widget) {
  const auto flags = widget.get_state_flags();
  if ((flags & Gtk::STATE_FLAG_FOCUSED) != static_cast<Gtk::StateFlags>(0)) {
    return true;
  }
  if (widget.has_focus() || widget.is_focus()) {
    return true;
  }
  if (auto* top = dynamic_cast<Gtk::Window*>(widget.get_toplevel())) {
    if (Gtk::Widget* focus = top->get_focus()) {
      if (focus == &widget || widget.is_ancestor(*focus) || focus->is_ancestor(widget)) {
        return true;
      }
    }
  }
  bool focused = false;
  if (auto* container = dynamic_cast<Gtk::Container*>(&widget)) {
    container->forall([&](Gtk::Widget& child) {
      if (!focused && widget_focused(child)) {
        focused = true;
      }
    });
  }
  return focused;
}

void stroke_focus_ring(const Cairo::RefPtr<Cairo::Context>& cr, double x, double y, double w,
                       double h, double radius) {
  rounded_rect(cr, x, y, w, h, radius);
  cr->set_source_rgba(1, 1, 1, 0.9);
  cr->set_line_width(3.0);
  cr->stroke();
  rounded_rect(cr, x + 1.2, y + 1.2, w - 2.4, h - 2.4, std::max(1.0, radius - 1.2));
  cr->set_source_rgb(0.22, 0.40, 0.62);
  cr->set_line_width(2.0);
  cr->stroke();
}

void fade_combo_children(Gtk::ComboBox& combo) {
  if (combo.get_data("lcos-combo-faded") != nullptr) {
    return;
  }
  combo.set_data("lcos-combo-faded", combo.gobj());
  combo.forall ([](Gtk::Widget& child) { child.set_opacity(0); });
}

bool draw_switch(Gtk::Switch& sw, const Cairo::RefPtr<Cairo::Context>& cr) {
  auto* paint = static_cast<SwitchPaint*>(sw.get_data("lcos-switch-paint"));
  if (paint == nullptr) {
    return false;
  }
  const double width = std::max(1, sw.get_allocated_width());
  const double height = std::max(1, sw.get_allocated_height());
  const double t = std::clamp(paint->shown, 0.0, 1.0);
  const bool enabled = sw.get_sensitive();
  cr->save();
  if (!enabled) {
    cr->push_group();
  }
  const double track_r = height / 2.0;
  rounded_rect(cr, 0.5, 0.5, width - 1, height - 1, track_r);
  const double off_r = 217.0 / 255.0;
  const double off_g = 214.0 / 255.0;
  const double off_b = 210.0 / 255.0;
  const double on_r = 126.0 / 255.0;
  const double on_g = 163.0 / 255.0;
  const double on_b = 212.0 / 255.0;
  cr->set_source_rgb(off_r + (on_r - off_r) * t, off_g + (on_g - off_g) * t,
                     off_b + (on_b - off_b) * t);
  cr->fill_preserve();
  cr->set_source_rgb(t > 0.5 ? 82.0 / 255.0 : 143.0 / 255.0, t > 0.5 ? 121.0 / 255.0 : 138.0 / 255.0,
                     t > 0.5 ? 173.0 / 255.0 : 132.0 / 255.0);
  cr->set_line_width(1);
  cr->stroke();

  const double pad = 2.0;
  const double knob = std::max(4.0, height - pad * 2.0);
  const double x = pad + t * (width - knob - pad * 2.0);
  const double y = (height - knob) / 2.0;
  cr->arc(x + knob / 2.0, y + knob / 2.0, knob / 2.0 - 0.5, 0, 2 * kPi);
  cr->set_source_rgb(1, 1, 1);
  cr->fill_preserve();
  cr->set_source_rgba(0, 0, 0, 0.22);
  cr->set_line_width(1);
  cr->stroke();
  if (widget_focused(sw)) {
    // The blue ring disappears on a switch that is on. Draw a light ring
    // against the blue track, and the usual blue ring against the grey one.
    if (t > 0.45) {
      rounded_rect(cr, 1.0, 1.0, width - 2.0, height - 2.0, track_r);
      cr->set_source_rgb(1, 1, 1);
      cr->set_line_width(2.6);
      cr->stroke();
      rounded_rect(cr, 2.4, 2.4, width - 4.8, height - 4.8, std::max(1.0, track_r - 2.0));
      cr->set_source_rgb(0.12, 0.16, 0.22);
      cr->set_line_width(1.2);
      cr->stroke();
    } else {
      stroke_focus_ring(cr, 1.0, 1.0, width - 2.0, height - 2.0, track_r);
    }
  }
  if (!enabled) {
    cr->pop_group_to_source();
    cr->paint_with_alpha(0.55);
  }
  cr->restore();
  return true;
}

void animate_switch(Gtk::Switch& sw) {
  auto* paint = static_cast<SwitchPaint*>(sw.get_data("lcos-switch-paint"));
  if (paint == nullptr) {
    return;
  }
  const double target = sw.get_active() ? 1.0 : 0.0;
  if (std::fabs(paint->shown - target) < 0.02) {
    paint->shown = target;
    if (paint->timer != 0) {
      g_source_remove(paint->timer);
      paint->timer = 0;
    }
    sw.queue_draw();
    return;
  }
  if (paint->timer != 0) {
    return;
  }
  paint->timer = g_timeout_add(16, [](gpointer data) -> gboolean {
    auto* widget = static_cast<Gtk::Switch*>(data);
    auto* state = static_cast<SwitchPaint*>(widget->get_data("lcos-switch-paint"));
    if (state == nullptr) {
      return G_SOURCE_REMOVE;
    }
    const double target = widget->get_active() ? 1.0 : 0.0;
    state->shown += (target - state->shown) * 0.45;
    if (std::fabs(state->shown - target) < 0.02) {
      state->shown = target;
      state->timer = 0;
      widget->queue_draw();
      return G_SOURCE_REMOVE;
    }
    widget->queue_draw();
    return G_SOURCE_CONTINUE;
  }, &sw);
}

void install_switch(Gtk::Switch& sw) {
  if (sw.get_data("lcos-switch-paint") != nullptr) {
    return;
  }
  auto* paint = new SwitchPaint;
  paint->shown = sw.get_active() ? 1.0 : 0.0;
  sw.set_data("lcos-switch-paint", paint, free_switch_paint);
  sw.set_size_request(46, 22);
  sw.signal_state_flags_changed().connect([&sw](Gtk::StateFlags) { sw.queue_draw(); });
  sw.property_active().signal_changed().connect([&sw] { animate_switch(sw); });
  sw.signal_draw().connect(
      [&sw](const Cairo::RefPtr<Cairo::Context>& cr) { return draw_switch(sw, cr); }, false);
}

bool draw_combo(Gtk::ComboBox& combo, const Cairo::RefPtr<Cairo::Context>& cr) {
  fade_combo_children(combo);
  const double width = std::max(1, combo.get_allocated_width());
  const double height = std::max(1, combo.get_allocated_height());
  const bool enabled = combo.get_sensitive();
  const bool focused = widget_focused(combo);
  Gdk::RGBA base;
  Gdk::RGBA text_color;
  Gdk::RGBA fg;
  base.set_rgba(1, 1, 1, 1);
  text_color.set_rgba(0.1, 0.1, 0.1, 1);
  fg.set_rgba(0.45, 0.43, 0.4, 1);
  auto style = combo.get_style_context();
  style->lookup_color("theme_base_color", base);
  style->lookup_color("theme_text_color", text_color);
  style->lookup_color("theme_fg_color", fg);
  if (!enabled) {
    text_color.set_alpha(0.55);
  }
  cr->save();
  rounded_rect(cr, 0.5, 0.5, width - 1, height - 1, 4);
  auto fill = Cairo::LinearGradient::create(0, 0, 0, height);
  const double dim = enabled ? 0.0 : 0.18;
  auto mix = [](double from, double toward, double amount) {
    return from + (toward - from) * amount;
  };
  fill->add_color_stop_rgb(0, mix(base.get_red(), fg.get_red(), dim),
                           mix(base.get_green(), fg.get_green(), dim),
                           mix(base.get_blue(), fg.get_blue(), dim));
  fill->add_color_stop_rgb(1, mix(base.get_red(), fg.get_red(), dim + 0.06),
                           mix(base.get_green(), fg.get_green(), dim + 0.06),
                           mix(base.get_blue(), fg.get_blue(), dim + 0.06));
  cr->set_source(fill);
  cr->fill_preserve();
  if (focused) {
    cr->set_source_rgb(0.22, 0.40, 0.62);
    cr->set_line_width(2.4);
  } else {
    cr->set_source_rgb(155.0 / 255.0, 150.0 / 255.0, 143.0 / 255.0);
    cr->set_line_width(1);
  }
  cr->stroke();
  if (focused) {
    stroke_focus_ring(cr, 2.0, 2.0, width - 4.0, height - 4.0, 3.0);
  }

  const double chevron_x = width - 16;
  const double chevron_y = height / 2.0;
  cr->set_line_width(1.6);
  cr->set_line_cap(Cairo::LINE_CAP_ROUND);
  cr->set_line_join(Cairo::LINE_JOIN_ROUND);
  cr->move_to(chevron_x - 4, chevron_y - 2);
  cr->line_to(chevron_x, chevron_y + 2.5);
  cr->line_to(chevron_x + 4, chevron_y - 2);
  cr->set_source_rgba(text_color.get_red(), text_color.get_green(), text_color.get_blue(),
                      enabled ? 0.9 : 0.45);
  cr->stroke();

  Glib::ustring text;
  if (auto* labeled = dynamic_cast<Gtk::ComboBoxText*>(&combo)) {
    text = labeled->get_active_text();
  }
  if (!text.empty()) {
    auto layout = combo.create_pango_layout(text);
    const int text_width = static_cast<int>(std::max(8.0, width - 36.0) * PANGO_SCALE);
    layout->set_ellipsize(Pango::ELLIPSIZE_END);
    layout->set_width(text_width);
    int unused = 0;
    int text_height = 0;
    layout->get_pixel_size(unused, text_height);
    cr->move_to(10, std::max(0.0, (height - text_height) / 2.0));
    cr->set_source_rgba(text_color.get_red(), text_color.get_green(), text_color.get_blue(),
                        enabled ? 1.0 : 0.55);
    layout->show_in_cairo_context(cr);
  }
  cr->restore();
  return true;
}

void install_combo(Gtk::ComboBox& combo) {
  if (combo.get_data("lcos-combo-draw") != nullptr) {
    return;
  }
  combo.set_data("lcos-combo-draw", combo.gobj());
  combo.set_can_focus(true);
  combo.set_size_request(-1, 28);
  for (auto* cell : combo.get_cells()) {
    if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cell)) {
      text->property_ellipsize() = Pango::ELLIPSIZE_END;
      text->property_width_chars() = 18;
    }
  }
  if (auto accessible = combo.get_accessible()) {
    accessible->set_role(Atk::ROLE_COMBO_BOX);
  }
  combo.signal_state_flags_changed().connect([&combo](Gtk::StateFlags) { combo.queue_draw(); });
  combo.signal_changed().connect([&combo] { combo.queue_draw(); });
  combo.signal_draw().connect(
      [&combo](const Cairo::RefPtr<Cairo::Context>& cr) { return draw_combo(combo, cr); }, false);
}

void dress_scroll(Gtk::ScrolledWindow& scroll) {
  Gtk::Widget* child = scroll.get_child();
  if (auto* viewport = dynamic_cast<Gtk::Viewport*>(child)) {
    child = viewport->get_child();
  }
  if (dynamic_cast<Gtk::TreeView*>(child) != nullptr) {
    scroll.set_shadow_type(Gtk::SHADOW_NONE);
    scroll.get_style_context()->add_class("lcos-list");
  }
}

void dress(Gtk::Widget& widget) {
  if (GTK_IS_FILE_CHOOSER_BUTTON(widget.gobj())) {
    return;
  }
  if (auto* combo = dynamic_cast<Gtk::ComboBox*>(&widget)) {
    install_combo(*combo);
  }
  if (auto* sw = dynamic_cast<Gtk::Switch*>(&widget)) {
    install_switch(*sw);
  }
  if (auto* scroll = dynamic_cast<Gtk::ScrolledWindow*>(&widget)) {
    dress_scroll(*scroll);
  }
  if (auto* switcher = dynamic_cast<Gtk::StackSwitcher*>(&widget)) {
    switcher->get_style_context()->add_class("lcos-sections");
  }
}

gboolean on_realize(GSignalInvocationHint*, guint, const GValue* values, gpointer) {
  auto* gwidget = GTK_WIDGET(g_value_get_object(values));
  if (gwidget == nullptr) {
    return TRUE;
  }
  if (GTK_IS_FILE_CHOOSER_BUTTON(gwidget)) {
    return TRUE;
  }
  if (!GTK_IS_COMBO_BOX(gwidget) && !GTK_IS_SWITCH(gwidget) && !GTK_IS_SCROLLED_WINDOW(gwidget) &&
      !GTK_IS_STACK_SWITCHER(gwidget)) {
    return TRUE;
  }
  if (auto* widget = Glib::wrap(gwidget)) {
    dress(*widget);
  }
  return TRUE;
}

}  // namespace

void install_drawn_controls() {
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;
  g_signal_add_emission_hook(g_signal_lookup("realize", GTK_TYPE_WIDGET), 0, on_realize, nullptr,
                             nullptr);
}

}  // namespace lcos
