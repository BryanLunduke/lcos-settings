// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/window_page.hpp"

#include "logic/wm_shortcuts.hpp"
#include "ui_util.hpp"

#include <gdk/gdkkeysyms.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dialog.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/scrolledwindow.h>

#include <map>

namespace lcos {
namespace {

constexpr const char* kChannel = kWmChannel;

class GrabDialog : public Gtk::Dialog {
 public:
  explicit GrabDialog(Gtk::Window& parent) : Gtk::Dialog("Window shortcut", parent, true) {
    auto* label = Gtk::manage(new Gtk::Label("Press the key combination. Escape cancels."));
    label->set_margin_top(16);
    label->set_margin_bottom(16);
    label->set_margin_start(16);
    label->set_margin_end(16);
    get_content_area()->pack_start(*label, Gtk::PACK_SHRINK);
    add_button("Cancel", Gtk::RESPONSE_CANCEL);
    set_default_size(420, 120);
    show_all();
  }

  std::string accel;

 protected:
  bool on_key_press_event(GdkEventKey* event) override {
    if (event->keyval == GDK_KEY_Escape) {
      response(Gtk::RESPONSE_CANCEL);
      return true;
    }
    if (event->is_modifier != 0) {
      return true;
    }
    const auto mods = static_cast<Gdk::ModifierType>(event->state) &
                      Gtk::AccelGroup::get_default_mod_mask();
    accel = Gtk::AccelGroup::name(event->keyval, mods);
    if (accel.empty()) {
      return true;
    }
    response(Gtk::RESPONSE_OK);
    return true;
  }
};

void prepare_scale(Gtk::Scale& scale, double lower, double upper, double step) {
  scale.set_range(lower, upper);
  scale.set_increments(step, step * 10);
  scale.set_digits(0);
  scale.set_value_pos(Gtk::POS_RIGHT);
  scale.set_hexpand(true);
}

}  // namespace

WindowPage::WindowPage(XfconfStore& store, Gtk::Window& parent)
    : store_(store),
      parent_(parent),
      click_focus_("Click to focus"),
      follow_focus_("Focus follows mouse"),
      focus_delay_(Gtk::ORIENTATION_HORIZONTAL),
      raise_delay_(Gtk::ORIENTATION_HORIZONTAL),
      snap_width_(Gtk::ORIENTATION_HORIZONTAL) {
  follow_focus_.join_group(click_focus_);
  prepare_scale(focus_delay_, 5, 2000, 10);
  prepare_scale(raise_delay_, 5, 2000, 10);
  prepare_scale(snap_width_, 5, 100, 1);
  focus_new_.set_label("Automatically give focus to newly created windows");
  raise_on_focus_.set_label("Automatically raise windows when they receive focus");
  raise_on_click_.set_label("Raise window when clicking inside the application window");
  snap_border_.set_label("Snap to screen borders");
  snap_windows_.set_label("Snap to other windows");
  box_move_.set_label("While moving");
  box_resize_.set_label("While resizing");
  double_click_.append("shade", "Shade window");
  double_click_.append("hide", "Hide window");
  double_click_.append("maximize", "Maximize window");
  double_click_.append("fill", "Fill window");
  double_click_.append("above", "Always on top");
  double_click_.append("none", "Nothing");

  pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);

  auto* focus = make_vbox(8);
  focus->set_border_width(0);
  FormGrid model(220);
  model.add_control(click_focus_);
  model.add_control(follow_focus_);
  model.add("Delay before focus", focus_delay_);
  focus->pack_start(*make_frame("Focus model", model.grid()), Gtk::PACK_SHRINK);
  FormGrid created(220);
  created.add_control(focus_new_);
  focus->pack_start(*make_frame("New window focus", created.grid()), Gtk::PACK_SHRINK);
  FormGrid raise(220);
  raise.add_control(raise_on_focus_);
  raise.add("Delay before raising", raise_delay_);
  focus->pack_start(*make_frame("Raise on focus", raise.grid()), Gtk::PACK_SHRINK);
  FormGrid click(220);
  click.add_control(raise_on_click_);
  focus->pack_start(*make_frame("Raise on click", click.grid()), Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*focus), "Focus");

  auto* keys = make_vbox(8);
  keys->set_border_width(0);
  auto* caption = make_label("Shortcuts for window manager actions.");
  caption->set_line_wrap(true);
  keys->pack_start(*caption, Gtk::PACK_SHRINK);
  shortcuts_ = Gtk::ListStore::create(shortcut_columns_);
  shortcuts_view_.set_model(shortcuts_);
  shortcuts_view_.append_column("Action", shortcut_columns_.label);
  shortcuts_view_.append_column("Shortcut", shortcut_columns_.accel);
  shortcuts_view_.set_headers_visible(true);
  shortcuts_view_.get_column(0)->set_expand(true);
  shortcuts_view_.get_column(0)->set_resizable(true);
  auto* key_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*key_scroll);
  key_scroll->set_shadow_type(Gtk::SHADOW_IN);
  key_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  key_scroll->set_vexpand(true);
  key_scroll->set_min_content_height(120);
  key_scroll->add(shortcuts_view_);
  keys->pack_start(*key_scroll, Gtk::PACK_EXPAND_WIDGET);
  auto* buttons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  auto* edit = Gtk::manage(new Gtk::Button("Edit"));
  auto* clear = Gtk::manage(new Gtk::Button("Clear"));
  auto* reset = Gtk::manage(new Gtk::Button("Reset to defaults"));
  buttons->pack_start(*edit, Gtk::PACK_SHRINK);
  buttons->pack_start(*clear, Gtk::PACK_SHRINK);
  buttons->pack_start(*reset, Gtk::PACK_SHRINK);
  keys->pack_start(*buttons, Gtk::PACK_SHRINK);
  notebook_.append_page(*keys, "Keyboard");

  auto* advanced = make_vbox(8);
  advanced->set_border_width(0);
  FormGrid snap(180);
  snap.add_control(snap_border_);
  snap.add_control(snap_windows_);
  snap.add("Distance", snap_width_);
  advanced->pack_start(*make_frame("Window snapping", snap.grid()), Gtk::PACK_SHRINK);
  FormGrid hidden(180);
  hidden.add_control(box_move_);
  hidden.add_control(box_resize_);
  advanced->pack_start(*make_frame("Hide content of windows", hidden.grid()), Gtk::PACK_SHRINK);
  FormGrid action(180);
  action.add("Action", double_click_);
  advanced->pack_start(*make_frame("Double click action", action.grid()), Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*advanced), "Advanced");

  click_focus_.signal_toggled().connect([this] {
    if (freeze_ > 0 || !click_focus_.get_active()) {
      return;
    }
    store_.set_bool(kChannel, "/general/click_to_focus", true);
    focus_delay_.set_sensitive(false);
  });
  follow_focus_.signal_toggled().connect([this] {
    if (freeze_ > 0 || !follow_focus_.get_active()) {
      return;
    }
    store_.set_bool(kChannel, "/general/click_to_focus", false);
    focus_delay_.set_sensitive(true);
  });
  focus_delay_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_int(kChannel, "/general/focus_delay", static_cast<int>(focus_delay_.get_value()));
  });
  focus_new_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/general/focus_new", focus_new_.get_active());
  });
  raise_on_focus_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/general/raise_on_focus", raise_on_focus_.get_active());
  });
  raise_delay_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_int(kChannel, "/general/raise_delay", static_cast<int>(raise_delay_.get_value()));
  });
  raise_on_click_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/general/raise_on_click", raise_on_click_.get_active());
  });
  snap_border_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/general/snap_to_border", snap_border_.get_active());
  });
  snap_windows_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/general/snap_to_windows", snap_windows_.get_active());
  });
  snap_width_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_int(kChannel, "/general/snap_width", static_cast<int>(snap_width_.get_value()));
  });
  box_move_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/general/box_move", box_move_.get_active());
  });
  box_resize_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/general/box_resize", box_resize_.get_active());
  });
  double_click_.signal_changed().connect([this] {
    if (freeze_ > 0 || double_click_.get_active_id().empty()) {
      return;
    }
    store_.set_string(kChannel, "/general/double_click_action", double_click_.get_active_id());
  });
  edit->signal_clicked().connect(sigc::mem_fun(*this, &WindowPage::edit_shortcut));
  clear->signal_clicked().connect(sigc::mem_fun(*this, &WindowPage::clear_shortcut));
  reset->signal_clicked().connect(sigc::mem_fun(*this, &WindowPage::reset_shortcuts));
  shortcuts_view_.signal_row_activated().connect(
      [this](const Gtk::TreeModel::Path&, Gtk::TreeViewColumn*) { edit_shortcut(); });

  load();
  load_shortcuts();
}

void WindowPage::on_page_shown() { load(); }

void WindowPage::load() {
  Freeze freeze(freeze_);
  const bool click = store_.get_bool(kChannel, "/general/click_to_focus", true);
  if (click) {
    click_focus_.set_active(true);
  } else {
    follow_focus_.set_active(true);
  }
  focus_delay_.set_sensitive(!click);
  focus_delay_.set_value(store_.get_int(kChannel, "/general/focus_delay", 250));
  focus_new_.set_active(store_.get_bool(kChannel, "/general/focus_new", true));
  raise_on_focus_.set_active(store_.get_bool(kChannel, "/general/raise_on_focus", false));
  raise_delay_.set_value(store_.get_int(kChannel, "/general/raise_delay", 250));
  raise_on_click_.set_active(store_.get_bool(kChannel, "/general/raise_on_click", true));
  snap_border_.set_active(store_.get_bool(kChannel, "/general/snap_to_border", true));
  snap_windows_.set_active(store_.get_bool(kChannel, "/general/snap_to_windows", false));
  snap_width_.set_value(store_.get_int(kChannel, "/general/snap_width", 10));
  box_move_.set_active(store_.get_bool(kChannel, "/general/box_move", false));
  box_resize_.set_active(store_.get_bool(kChannel, "/general/box_resize", false));
  const std::string action =
      store_.get_string(kChannel, "/general/double_click_action", "maximize");
  double_click_.set_active_id(action);
  if (double_click_.get_active_row_number() < 0) {
    double_click_.set_active_id("maximize");
  }
}

std::map<std::string, std::string> WindowPage::shortcut_values() const {
  std::map<std::string, std::string> values;
  for (const char* base : {kWmShortcutCustomBase, kWmShortcutDefaultBase}) {
    for (const auto& entry : store_.list(kWmShortcutChannel, base)) {
      if (entry.second.type == PropType::String) {
        values[entry.first] = entry.second.s;
      }
    }
  }
  return values;
}

void WindowPage::load_shortcuts() {
  shortcuts_->clear();
  for (const auto& binding : merge_wm_shortcuts(shortcut_values())) {
    auto row = shortcuts_->append();
    (*row)[shortcut_columns_.feature] = binding.feature;
    (*row)[shortcut_columns_.label] = binding.label;
    (*row)[shortcut_columns_.accel] = binding.accel;
  }
}

void WindowPage::apply_shortcut(const std::string& feature, const std::string& accel) {
  for (const auto& edit : plan_wm_shortcut(shortcut_values(), feature, accel)) {
    if (edit.action == WmShortcutAction::Reset) {
      store_.reset(kWmShortcutChannel, edit.property, false);
    } else {
      store_.set_string(kWmShortcutChannel, edit.property, edit.value);
    }
  }
  load_shortcuts();
}

void WindowPage::edit_shortcut() {
  auto iter = shortcuts_view_.get_selection()->get_selected();
  if (!iter) {
    return;
  }
  const std::string feature = static_cast<Glib::ustring>((*iter)[shortcut_columns_.feature]);
  const std::string label = static_cast<Glib::ustring>((*iter)[shortcut_columns_.label]);
  GrabDialog grab(parent_);
  grab.set_title(label);
  if (grab.run() != Gtk::RESPONSE_OK || grab.accel.empty()) {
    return;
  }
  apply_shortcut(feature, grab.accel);
}

void WindowPage::clear_shortcut() {
  auto iter = shortcuts_view_.get_selection()->get_selected();
  if (!iter) {
    return;
  }
  const std::string feature = static_cast<Glib::ustring>((*iter)[shortcut_columns_.feature]);
  apply_shortcut(feature, "");
}

void WindowPage::reset_shortcuts() {
  Gtk::MessageDialog dialog(parent_, "Reset window manager shortcuts?", false,
                            Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO, true);
  dialog.set_secondary_text("Custom window-manager shortcuts from this visit can be restored with Cancel.");
  if (dialog.run() != Gtk::RESPONSE_YES) {
    return;
  }
  store_.reset(kWmShortcutChannel, kWmShortcutCustomBase, true);
  load_shortcuts();
}

void WindowPage::prepare_screenshot(int index) {
  if (index >= 0 && index < notebook_.get_n_pages()) {
    notebook_.set_current_page(index);
  }
}

std::string WindowPage::screenshot_suffix(int index) const {
  switch (index) {
    case 0:
      return "focus";
    case 1:
      return "keyboard";
    case 2:
      return "advanced";
    default:
      return {};
  }
}

}  // namespace lcos
