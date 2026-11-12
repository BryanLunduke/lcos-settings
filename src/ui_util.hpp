// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_UI_UTIL_HPP
#define LCOS_UI_UTIL_HPP

#include <gtkmm/box.h>
#include <gtkmm/combobox.h>
#include <gtkmm/container.h>
#include <gtkmm/frame.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/widget.h>
#include <gtkmm/window.h>

#include <gtk/gtk.h>

namespace lcos {

inline Gtk::Label* make_label(const Glib::ustring& text) {
  auto* label = Gtk::manage(new Gtk::Label(text));
  label->set_halign(Gtk::ALIGN_START);
  label->set_valign(Gtk::ALIGN_CENTER);
  return label;
}

inline Gtk::Box* make_vbox(int spacing = 8) {
  auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, spacing));
  return box;
}

// A page body that can shrink with the window. The scrollbar appears only
// when the contents do not fit. The minimum stays small so the window can
// be about half the default height.
inline void prepare_page_scroll(Gtk::ScrolledWindow& scroll) {
  scroll.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  scroll.set_shadow_type(Gtk::SHADOW_NONE);
  scroll.set_overlay_scrolling(false);
  scroll.set_propagate_natural_height(false);
  scroll.set_propagate_natural_width(false);
  scroll.set_min_content_height(0);
  scroll.set_min_content_width(0);
}

inline Gtk::ScrolledWindow* make_page_scroll(Gtk::Widget& child) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  scroll->add(child);
  return scroll;
}

// A wheel over an unfocused spin button or combo used to change its value
// while the page scrolled. Eat the control's handler so the scroll reaches
// the page instead. A focused control still changes.
inline void guard_scroll_unless_focused(Gtk::Widget& widget) {
  if (widget.get_data("lcos-scroll-guard") != nullptr) {
    return;
  }
  widget.set_data("lcos-scroll-guard", widget.gobj());
  widget.add_events(Gdk::SCROLL_MASK | Gdk::SMOOTH_SCROLL_MASK);
  widget.signal_scroll_event().connect([&widget](GdkEventScroll*) {
    Gtk::Widget* focus = nullptr;
    if (auto* window = dynamic_cast<Gtk::Window*>(widget.get_toplevel())) {
      focus = window->get_focus();
    }
    const bool focused =
        widget.has_focus() || widget.is_focus() ||
        (focus != nullptr && (focus == &widget || focus->is_ancestor(widget)));
    if (!focused) {
      g_signal_stop_emission_by_name(widget.gobj(), "scroll-event");
    }
    return false;
  });
}

inline void guard_scroll_tree(Gtk::Widget& widget) {
  if (dynamic_cast<Gtk::SpinButton*>(&widget) != nullptr ||
      dynamic_cast<Gtk::ComboBox*>(&widget) != nullptr) {
    guard_scroll_unless_focused(widget);
  }
  if (auto* container = dynamic_cast<Gtk::Container*>(&widget)) {
    container->foreach ([&](Gtk::Widget& child) { guard_scroll_tree(child); });
  }
}

// Heading plus a light rule, in place of a boxed frame, so every page uses
// the same group chrome.
inline Gtk::Box* make_frame(const Glib::ustring& title, Gtk::Widget& child) {
  auto* frame = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
  frame->get_style_context()->add_class("lcos-group");
  frame->set_margin_top(18);
  auto* head = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
  auto* label = Gtk::manage(new Gtk::Label(title));
  label->set_halign(Gtk::ALIGN_START);
  label->get_style_context()->add_class("lcos-group-title");
  auto* rule = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
  rule->set_hexpand(true);
  rule->set_valign(Gtk::ALIGN_CENTER);
  head->pack_start(*label, Gtk::PACK_SHRINK);
  head->pack_start(*rule, Gtk::PACK_EXPAND_WIDGET);
  child.set_margin_start(12);
  child.set_margin_end(2);
  child.set_margin_top(12);
  child.set_margin_bottom(0);
  frame->pack_start(*head, Gtk::PACK_SHRINK);
  frame->pack_start(child, Gtk::PACK_SHRINK);
  return frame;
}

class FormGrid {
 public:
  explicit FormGrid(int label_width = 168) : label_width_(label_width) {
    grid_ = Gtk::manage(new Gtk::Grid());
    grid_->set_row_spacing(6);
    grid_->set_column_spacing(12);
    grid_->set_margin_top(2);
  }

  Gtk::Grid& grid() { return *grid_; }

  void add(const Glib::ustring& caption, Gtk::Widget& widget) {
    auto* label = Gtk::manage(new Gtk::Label(caption));
    label->set_halign(Gtk::ALIGN_START);
    label->set_valign(Gtk::ALIGN_CENTER);
    label->set_xalign(0);
    if (caption.find('_') != Glib::ustring::npos) {
      label->set_use_underline(true);
    }
    label->set_mnemonic_widget(widget);
    label->set_size_request(label_width_, -1);
    // A caption with an explicit line break keeps a parenthetical on its
    // own line instead of wrapping in the middle of the phrase.
    if (caption.find('\n') != Glib::ustring::npos) {
      label->set_line_wrap(false);
    } else {
      label->set_line_wrap(true);
      label->set_line_wrap_mode(Pango::WRAP_WORD);
      label->set_max_width_chars(32);
    }
    widget.set_hexpand(true);
    widget.set_halign(Gtk::ALIGN_FILL);
    guard_scroll_unless_focused(widget);
    grid_->attach(*label, 0, row_, 1, 1);
    grid_->attach(widget, 1, row_, 1, 1);
    ++row_;
  }

  void add_full(Gtk::Widget& widget) {
    widget.set_hexpand(true);
    guard_scroll_unless_focused(widget);
    grid_->attach(widget, 0, row_, 2, 1);
    ++row_;
  }

  // Checkbox or button aligned with the control column, not the labels.
  void add_control(Gtk::Widget& widget) {
    widget.set_hexpand(true);
    widget.set_halign(Gtk::ALIGN_START);
    guard_scroll_unless_focused(widget);
    grid_->attach(widget, 1, row_, 1, 1);
    ++row_;
  }

 private:
  Gtk::Grid* grid_ = nullptr;
  int row_ = 0;
  int label_width_ = 168;
};

struct Freeze {
  int& depth;
  explicit Freeze(int& depth) : depth(depth) { ++depth; }
  ~Freeze() { --depth; }
};

}  // namespace lcos

#endif
