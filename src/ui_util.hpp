// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_UI_UTIL_HPP
#define LCOS_UI_UTIL_HPP

#include <gtkmm/box.h>
#include <gtkmm/frame.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

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

inline Gtk::Frame* make_frame(const Glib::ustring& title, Gtk::Widget& child) {
  auto* frame = Gtk::manage(new Gtk::Frame(title));
  frame->set_margin_bottom(12);
  child.set_margin_start(12);
  child.set_margin_end(12);
  child.set_margin_top(8);
  child.set_margin_bottom(10);
  frame->add(child);
  return frame;
}

class FormGrid {
 public:
  explicit FormGrid(int label_width = 168) : label_width_(label_width) {
    grid_ = Gtk::manage(new Gtk::Grid());
    grid_->set_row_spacing(8);
    grid_->set_column_spacing(12);
    grid_->set_margin_top(2);
  }

  Gtk::Grid& grid() { return *grid_; }

  void add(const Glib::ustring& caption, Gtk::Widget& widget) {
    auto* label = make_label(caption);
    label->set_size_request(label_width_, -1);
    label->set_line_wrap(true);
    label->set_xalign(0);
    label->set_max_width_chars(28);
    widget.set_hexpand(true);
    widget.set_halign(Gtk::ALIGN_FILL);
    grid_->attach(*label, 0, row_, 1, 1);
    grid_->attach(widget, 1, row_, 1, 1);
    ++row_;
  }

  void add_full(Gtk::Widget& widget) {
    widget.set_hexpand(true);
    grid_->attach(widget, 0, row_, 2, 1);
    ++row_;
  }

  // Checkbox or button aligned with the control column, not the labels.
  void add_control(Gtk::Widget& widget) {
    widget.set_hexpand(true);
    widget.set_halign(Gtk::ALIGN_START);
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
