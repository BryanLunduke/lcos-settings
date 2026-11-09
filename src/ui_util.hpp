// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_UI_UTIL_HPP
#define LCOS_UI_UTIL_HPP

#include <gtkmm/box.h>
#include <gtkmm/frame.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>

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
  FormGrid() {
    grid_ = Gtk::manage(new Gtk::Grid());
    grid_->set_row_spacing(8);
    grid_->set_column_spacing(12);
  }

  Gtk::Grid& grid() { return *grid_; }

  void add(const Glib::ustring& caption, Gtk::Widget& widget) {
    auto* label = make_label(caption);
    widget.set_hexpand(true);
    grid_->attach(*label, 0, row_, 1, 1);
    grid_->attach(widget, 1, row_, 1, 1);
    ++row_;
  }

  void add_full(Gtk::Widget& widget) {
    widget.set_hexpand(true);
    grid_->attach(widget, 0, row_, 2, 1);
    ++row_;
  }

 private:
  Gtk::Grid* grid_ = nullptr;
  int row_ = 0;
};

struct Freeze {
  int& depth;
  explicit Freeze(int& depth) : depth(depth) { ++depth; }
  ~Freeze() { --depth; }
};

}  // namespace lcos

#endif
