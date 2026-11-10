// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/accessibility_page.hpp"

#include "ui_util.hpp"

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

namespace lcos {
namespace {

constexpr const char* kAccess = "accessibility";
constexpr const char* kSession = "xfce4-session";

void prepare_spin(Gtk::SpinButton& spin, double lower, double upper, double step) {
  spin.set_range(lower, upper);
  spin.set_increments(step, step * 10);
  spin.set_digits(0);
  spin.set_numeric(true);
}

}  // namespace

AccessibilityPage::AccessibilityPage(XfconfStore& store) : store_(store) {
  assistive_.set_label("Enable assistive technologies");
  sticky_.set_label("Use sticky keys");
  sticky_lock_.set_label("Lock sticky keys");
  sticky_two_.set_label("Disable sticky keys if two keys are pressed");
  slow_.set_label("Use slow keys");
  bounce_.set_label("Use bounce keys");
  mouse_keys_.set_label("Use mouse emulation");
  find_cursor_.set_label("Show the pointer location when Control is pressed");
  prepare_spin(slow_delay_, 50, 1000, 10);
  prepare_spin(bounce_delay_, 50, 1000, 1);
  prepare_spin(mouse_delay_, 1, 1000, 10);
  prepare_spin(mouse_interval_, 10, 1000, 1);
  prepare_spin(mouse_time_, 100, 10000, 10);
  prepare_spin(mouse_speed_, 1, 2000, 10);
  prepare_spin(mouse_curve_, -1000, 1000, 10);

  pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);

  auto* technologies = make_vbox(8);
  technologies->set_border_width(12);
  auto* note = make_label(
      "When this is enabled, the session manager starts the applications required for screen "
      "readers and magnifiers the next time you log in.");
  note->set_line_wrap(true);
  technologies->pack_start(*note, Gtk::PACK_SHRINK);
  technologies->pack_start(assistive_, Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*technologies), "Assistive Technologies");

  auto* keyboard = make_vbox(8);
  keyboard->set_border_width(12);
  auto* sticky_box = make_vbox(6);
  sticky_box->pack_start(sticky_, Gtk::PACK_SHRINK);
  sticky_box->pack_start(sticky_lock_, Gtk::PACK_SHRINK);
  sticky_box->pack_start(sticky_two_, Gtk::PACK_SHRINK);
  keyboard->pack_start(*make_frame("Sticky keys", *sticky_box), Gtk::PACK_SHRINK);
  FormGrid slow_form(180);
  slow_form.add_control(slow_);
  slow_form.add("Acceptance delay (ms)", slow_delay_);
  keyboard->pack_start(*make_frame("Slow keys", slow_form.grid()), Gtk::PACK_SHRINK);
  FormGrid bounce_form(180);
  bounce_form.add_control(bounce_);
  bounce_form.add("Keystroke delay (ms)", bounce_delay_);
  keyboard->pack_start(*make_frame("Bounce keys", bounce_form.grid()), Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*keyboard), "Keyboard");

  auto* mouse = make_vbox(8);
  mouse->set_border_width(12);
  auto* mouse_note = make_label("The pointer can be moved with the numeric keypad.");
  mouse_note->set_line_wrap(true);
  mouse->pack_start(*mouse_note, Gtk::PACK_SHRINK);
  FormGrid mouse_form(200);
  mouse_form.add_control(mouse_keys_);
  mouse_form.add("Acceleration delay (ms)", mouse_delay_);
  mouse_form.add("Repeat interval (ms)", mouse_interval_);
  mouse_form.add("Acceleration time (ms)", mouse_time_);
  mouse_form.add("Maximum speed (pixels/sec)", mouse_speed_);
  mouse_form.add("Acceleration profile", mouse_curve_);
  mouse->pack_start(*make_frame("Mouse emulation", mouse_form.grid()), Gtk::PACK_SHRINK);
  auto* find_note = make_label(
      "Shows where the pointer is. This is the Find Cursor option from the accessibility dialog.");
  find_note->set_line_wrap(true);
  mouse->pack_start(*find_note, Gtk::PACK_SHRINK);
  mouse->pack_start(find_cursor_, Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*mouse), "Mouse");

  assistive_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kSession, "/general/StartAssistiveTechnologies", assistive_.get_active());
  });
  auto bind_bool = [this](Gtk::CheckButton& check, const char* prop) {
    check.signal_toggled().connect([this, &check, prop] {
      if (freeze_ > 0) {
        return;
      }
      store_.set_bool(kAccess, prop, check.get_active());
    });
  };
  auto bind_int = [this](Gtk::SpinButton& spin, const char* prop) {
    spin.signal_value_changed().connect([this, &spin, prop] {
      if (freeze_ > 0) {
        return;
      }
      store_.set_int(kAccess, prop, spin.get_value_as_int());
    });
  };
  sticky_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kAccess, "/StickyKeys", sticky_.get_active());
    sticky_lock_.set_sensitive(sticky_.get_active());
    sticky_two_.set_sensitive(sticky_.get_active());
  });
  bind_bool(sticky_lock_, "/StickyKeys/LatchToLock");
  bind_bool(sticky_two_, "/StickyKeys/TwoKeysDisable");
  slow_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kAccess, "/SlowKeys", slow_.get_active());
    slow_delay_.set_sensitive(slow_.get_active());
  });
  bind_int(slow_delay_, "/SlowKeys/Delay");
  bounce_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kAccess, "/BounceKeys", bounce_.get_active());
    bounce_delay_.set_sensitive(bounce_.get_active());
  });
  bind_int(bounce_delay_, "/BounceKeys/Delay");
  mouse_keys_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kAccess, "/MouseKeys", mouse_keys_.get_active());
    const bool on = mouse_keys_.get_active();
    mouse_delay_.set_sensitive(on);
    mouse_interval_.set_sensitive(on);
    mouse_time_.set_sensitive(on);
    mouse_speed_.set_sensitive(on);
    mouse_curve_.set_sensitive(on);
  });
  bind_int(mouse_delay_, "/MouseKeys/Delay");
  bind_int(mouse_interval_, "/MouseKeys/Interval");
  bind_int(mouse_time_, "/MouseKeys/TimeToMax");
  bind_int(mouse_speed_, "/MouseKeys/MaxSpeed");
  bind_int(mouse_curve_, "/MouseKeys/Curve");
  bind_bool(find_cursor_, "/FindCursor");

  load();
}

void AccessibilityPage::load() {
  Freeze freeze(freeze_);
  assistive_.set_active(store_.get_bool(kSession, "/general/StartAssistiveTechnologies", false));
  sticky_.set_active(store_.get_bool(kAccess, "/StickyKeys", false));
  sticky_lock_.set_active(store_.get_bool(kAccess, "/StickyKeys/LatchToLock", false));
  sticky_two_.set_active(store_.get_bool(kAccess, "/StickyKeys/TwoKeysDisable", false));
  sticky_lock_.set_sensitive(sticky_.get_active());
  sticky_two_.set_sensitive(sticky_.get_active());
  slow_.set_active(store_.get_bool(kAccess, "/SlowKeys", false));
  slow_delay_.set_value(store_.get_int(kAccess, "/SlowKeys/Delay", 500));
  slow_delay_.set_sensitive(slow_.get_active());
  bounce_.set_active(store_.get_bool(kAccess, "/BounceKeys", false));
  bounce_delay_.set_value(store_.get_int(kAccess, "/BounceKeys/Delay", 500));
  bounce_delay_.set_sensitive(bounce_.get_active());
  mouse_keys_.set_active(store_.get_bool(kAccess, "/MouseKeys", false));
  mouse_delay_.set_value(store_.get_int(kAccess, "/MouseKeys/Delay", 150));
  mouse_interval_.set_value(store_.get_int(kAccess, "/MouseKeys/Interval", 20));
  mouse_time_.set_value(store_.get_int(kAccess, "/MouseKeys/TimeToMax", 3000));
  mouse_speed_.set_value(store_.get_int(kAccess, "/MouseKeys/MaxSpeed", 1000));
  mouse_curve_.set_value(store_.get_int(kAccess, "/MouseKeys/Curve", 0));
  const bool mouse = mouse_keys_.get_active();
  mouse_delay_.set_sensitive(mouse);
  mouse_interval_.set_sensitive(mouse);
  mouse_time_.set_sensitive(mouse);
  mouse_speed_.set_sensitive(mouse);
  mouse_curve_.set_sensitive(mouse);
  find_cursor_.set_active(store_.get_bool(kAccess, "/FindCursor", false));
}

void AccessibilityPage::on_page_shown() { load(); }

void AccessibilityPage::prepare_screenshot(int index) {
  if (index >= 0 && index < notebook_.get_n_pages()) {
    notebook_.set_current_page(index);
  }
}

std::string AccessibilityPage::screenshot_suffix(int index) const {
  switch (index) {
    case 0:
      return "technologies";
    case 1:
      return "keyboard";
    case 2:
      return "mouse";
    default:
      return {};
  }
}

}  // namespace lcos
