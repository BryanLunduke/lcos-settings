// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_UI_UTIL_HPP
#define LCOS_UI_UTIL_HPP

#include <gtkmm/cellrenderertext.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/colorbutton.h>
#include <gtkmm/combobox.h>
#include <gtkmm/container.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/frame.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/scale.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/stack.h>
#include <gtkmm/treeview.h>
#include <gtkmm/widget.h>
#include <gtkmm/window.h>

#include <algorithm>
#include <string>
#include <vector>

#include <gtk/gtk.h>

namespace lcos {

// One caption column for every page, so the controls start at the same x.
constexpr int kFormLabelWidth = 188;
constexpr int kDialogMargin = 12;
constexpr int kDialogSpacing = 8;

inline Gtk::Label* make_label(const Glib::ustring& text) {
  auto* label = Gtk::manage(new Gtk::Label(text));
  label->set_halign(Gtk::ALIGN_START);
  label->set_valign(Gtk::ALIGN_CENTER);
  label->set_xalign(0);
  return label;
}

// Help text indented with the group body, not the page edge.
inline Gtk::Label* make_note(const Glib::ustring& text) {
  auto* label = make_label(text);
  label->set_line_wrap(true);
  label->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
  label->set_max_width_chars(72);
  label->set_margin_start(12);
  label->get_style_context()->add_class("lcos-faint");
  return label;
}

inline void prepare_dialog_box(Gtk::Box& box) {
  box.set_margin_top(kDialogMargin);
  box.set_margin_bottom(kDialogMargin);
  box.set_margin_start(kDialogMargin);
  box.set_margin_end(kDialogMargin);
  box.set_spacing(kDialogSpacing);
}

inline void prepare_dialog(Gtk::Dialog& dialog) {
  prepare_dialog_box(*dialog.get_content_area());
  dialog.set_default_response(Gtk::RESPONSE_OK);
}

inline void configure_scale(Gtk::Scale& scale) {
  scale.set_value_pos(Gtk::POS_RIGHT);
  scale.set_hexpand(true);
  scale.set_halign(Gtk::ALIGN_FILL);
}

// The combo's natural width is its longest item. Cap that so a model name
// or a locale cannot push the page past the window.
inline void cap_combo_width(Gtk::ComboBox& combo) {
  combo.set_can_focus(true);
  combo.set_hexpand(true);
  for (auto* cell : combo.get_cells()) {
    if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cell)) {
      text->property_ellipsize() = Pango::ELLIPSIZE_END;
      text->property_width_chars() = 18;
    }
  }
}

inline void fit_tree_view(Gtk::TreeView& view) {
  view.set_hexpand(true);
  view.set_vexpand(true);
  for (auto* column : view.get_columns()) {
    if (column == nullptr) {
      continue;
    }
    column->set_resizable(true);
    column->set_expand(true);
    column->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
    column->set_min_width(48);
    column->set_fixed_width(120);
    for (auto* cell : column->get_cells()) {
      if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cell)) {
        text->property_ellipsize() = Pango::ELLIPSIZE_END;
      }
    }
  }
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
  // Overlay scrolling draws the bar on top of the page, so a trough is not
  // reserved when the page already fits, and the window can still shrink.
  scroll.set_overlay_scrolling(true);
  scroll.set_propagate_natural_height(false);
  scroll.set_propagate_natural_width(false);
  scroll.set_min_content_height(0);
  scroll.set_min_content_width(0);
  scroll.get_style_context()->add_class("lcos-page");
}

// Segmented section control used in place of a stock notebook. The pages that
// were not redrawn as cards still have several groups; this keeps that
// organization and matches the linked buttons on Display and Appearance.
class SectionNotebook : public Gtk::Box {
 public:
  SectionNotebook() : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0) {
    bar_.get_style_context()->add_class("lcos-sections");
    bar_.set_halign(Gtk::ALIGN_START);
    bar_.set_margin_bottom(10);
    stack_.set_vexpand(true);
    stack_.set_hexpand(true);
    pack_start(bar_, Gtk::PACK_SHRINK);
    pack_start(stack_, Gtk::PACK_EXPAND_WIDGET);
  }

  void append_page(Gtk::Widget& child, const Glib::ustring& title) {
    auto* button = Gtk::manage(new Gtk::Button(title));
    button->set_relief(Gtk::RELIEF_NONE);
    button->set_focus_on_click(true);
    const int index = static_cast<int>(pages_.size());
    button->signal_clicked().connect([this, index] { set_current_page(index); });
    bar_.pack_start(*button, Gtk::PACK_SHRINK);
    stack_.add(child, "page-" + std::to_string(index));
    pages_.push_back(&child);
    buttons_.push_back(button);
    if (pages_.size() == 1) {
      set_current_page(0);
    }
  }

  int get_n_pages() const { return static_cast<int>(pages_.size()); }

  int get_current_page() const { return current_; }

  void set_current_page(int index) {
    if (index < 0 || index >= get_n_pages()) {
      return;
    }
    current_ = index;
    stack_.set_visible_child(*pages_[static_cast<std::size_t>(index)]);
    for (int i = 0; i < get_n_pages(); ++i) {
      auto context = buttons_[static_cast<std::size_t>(i)]->get_style_context();
      if (i == index) {
        context->add_class("lcos-on");
      } else {
        context->remove_class("lcos-on");
      }
    }
  }

 private:
  Gtk::Box bar_{Gtk::ORIENTATION_HORIZONTAL, 0};
  Gtk::Stack stack_;
  std::vector<Gtk::Widget*> pages_;
  std::vector<Gtk::Button*> buttons_;
  int current_ = 0;
};

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
  rule->get_style_context()->add_class("lcos-rule");
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
  // The width argument is ignored so every page shares one caption column.
  explicit FormGrid(int = kFormLabelWidth) : label_width_(kFormLabelWidth) {
    grid_ = Gtk::manage(new Gtk::Grid());
    grid_->set_row_spacing(6);
    grid_->set_column_spacing(12);
    grid_->set_margin_top(2);
  }

  Gtk::Grid& grid() { return *grid_; }

  Gtk::Label* add(const Glib::ustring& caption, Gtk::Widget& widget) {
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
      label->set_width_chars(22);
      label->set_max_width_chars(22);
    }
    // Combos, scales, and text entries use the control column. A box is
    // filled so a scale or field packed inside it can still grow. Spins
    // and colour buttons stay at their natural width.
    if (dynamic_cast<Gtk::ComboBox*>(&widget) != nullptr ||
        dynamic_cast<Gtk::Scale*>(&widget) != nullptr ||
        dynamic_cast<Gtk::Entry*>(&widget) != nullptr ||
        dynamic_cast<Gtk::Box*>(&widget) != nullptr) {
      widget.set_hexpand(true);
      widget.set_halign(Gtk::ALIGN_FILL);
    } else {
      widget.set_hexpand(false);
      widget.set_halign(Gtk::ALIGN_START);
    }
    if (auto* combo = dynamic_cast<Gtk::ComboBox*>(&widget)) {
      cap_combo_width(*combo);
    }
    if (auto* scale = dynamic_cast<Gtk::Scale*>(&widget)) {
      configure_scale(*scale);
    }
    if (auto* spin = dynamic_cast<Gtk::SpinButton*>(&widget)) {
      if (spin->get_width_chars() < 6) {
        spin->set_width_chars(6);
      }
    }
    if (auto* color = dynamic_cast<Gtk::ColorButton*>(&widget)) {
      color->set_halign(Gtk::ALIGN_START);
    }
    if (auto* value = dynamic_cast<Gtk::Label*>(&widget)) {
      value->set_xalign(0);
      value->set_halign(Gtk::ALIGN_START);
    }
    if (widget.get_accessible()) {
      const Glib::ustring caption_text = label->get_text();
      if (!caption_text.empty()) {
        widget.get_accessible()->set_name(caption_text);
      }
    }
    guard_scroll_unless_focused(widget);
    grid_->attach(*label, 0, row_, 1, 1);
    grid_->attach(widget, 1, row_, 1, 1);
    ++row_;
    return label;
  }

  void add_full(Gtk::Widget& widget) {
    widget.set_hexpand(true);
    guard_scroll_unless_focused(widget);
    grid_->attach(widget, 0, row_, 2, 1);
    ++row_;
  }

  // Checkbox or button aligned with the control column, not the labels.
  // The spacer keeps the caption column at 188 px even when this grid has
  // no captions, so a checkbox-only group lines up with the other pages.
  void add_control(Gtk::Widget& widget) {
    auto* spacer = Gtk::manage(new Gtk::Box());
    spacer->set_size_request(label_width_, 1);
    spacer->set_hexpand(false);
    spacer->set_valign(Gtk::ALIGN_START);
    grid_->attach(*spacer, 0, row_, 1, 1);
    widget.set_hexpand(true);
    widget.set_halign(Gtk::ALIGN_START);
    guard_scroll_unless_focused(widget);
    grid_->attach(widget, 1, row_, 1, 1);
    ++row_;
  }

 private:
  Gtk::Grid* grid_ = nullptr;
  int row_ = 0;
  int label_width_ = 164;
};

struct Freeze {
  int& depth;
  explicit Freeze(int& depth) : depth(depth) { ++depth; }
  ~Freeze() { --depth; }
};

}  // namespace lcos

#endif
