// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/mouse_page.hpp"

#include "logic/names.hpp"
#include "logic/themes.hpp"
#include "ui_util.hpp"

#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

namespace lcos {
namespace {

constexpr const char* kDeviceEnabled = "Device Enabled";
constexpr const char* kTap = "libinput Tapping Enabled";
constexpr const char* kNatural = "libinput Natural Scrolling Enabled";
constexpr const char* kDisableTyping = "libinput Disable While Typing Enabled";
constexpr const char* kScrollMethod = "libinput Scroll Method Enabled";
constexpr const char* kClickMethod = "libinput Click Method Enabled";
constexpr const char* kMiddle = "libinput Middle Emulation Enabled";

}  // namespace

MousePage::MousePage(XfconfStore& store, PointerBackend& pointers)
    : store_(store),
      pointers_(pointers),
      right_handed_("Right-handed"),
      left_handed_("Left-handed"),
      acceleration_(Gtk::ORIENTATION_HORIZONTAL),
      threshold_(Gtk::ORIENTATION_HORIZONTAL) {
  left_handed_.join_group(right_handed_);
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  scroll->set_shadow_type(Gtk::SHADOW_NONE);
  auto* page = make_vbox(4);
  page->set_border_width(12);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  auto* device_box = make_vbox(8);
  FormGrid device_form;
  device_form.add("Device", device_);
  enabled_.set_label("Device enabled");
  device_form.add_full(enabled_);
  auto* hands = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  hands->pack_start(right_handed_, Gtk::PACK_SHRINK);
  hands->pack_start(left_handed_, Gtk::PACK_SHRINK);
  device_form.add("Buttons", *hands);
  acceleration_.set_range(0, 10);
  acceleration_.set_increments(0.1, 1);
  acceleration_.set_digits(1);
  acceleration_.set_value(5);
  threshold_.set_range(1, 30);
  threshold_.set_increments(1, 5);
  threshold_.set_digits(0);
  threshold_.set_value(4);
  reverse_.set_label("Reverse scrolling");
  device_form.add("Speed", acceleration_);
  device_form.add("Sensitivity", threshold_);
  device_form.add_full(reverse_);
  device_note_ = make_label("");
  device_note_->set_line_wrap(true);
  device_form.add_full(*device_note_);
  device_box->pack_start(device_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Device", *device_box), Gtk::PACK_SHRINK);

  auto* touch = make_vbox(8);
  tap_.set_label("Tap to click");
  natural_.set_label("Natural scrolling");
  disable_typing_.set_label("Disable this touchpad while typing");
  disable_typing_global_.set_label("Disable touchpads while typing (all devices)");
  typing_duration_.set_range(0.1, 5.0);
  typing_duration_.set_increments(0.1, 0.5);
  typing_duration_.set_digits(1);
  scroll_method_.append("two-finger", "Two-finger scrolling");
  scroll_method_.append("edge", "Edge scrolling");
  scroll_method_.append("button", "Button scrolling");
  scroll_method_.append("none", "No scrolling");
  click_method_.append("button-areas", "Bottom button areas");
  click_method_.append("clickfinger", "Click with two or three fingers");
  click_method_.append("none", "Off");
  middle_.set_label("Middle click emulation");
  FormGrid touch_form;
  touch_form.add_full(tap_);
  touch_form.add_full(natural_);
  touch_form.add_full(disable_typing_);
  touch_form.add("Scrolling", scroll_method_);
  touch_form.add("Click method", click_method_);
  touch_form.add_full(middle_);
  touch_form.add_full(disable_typing_global_);
  touch_form.add("Typing pause (seconds)", typing_duration_);
  touch->pack_start(touch_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Touchpad", *touch), Gtk::PACK_SHRINK);

  auto* behavior = make_vbox(8);
  double_time_.set_range(100, 2000);
  double_time_.set_increments(10, 50);
  double_time_.set_digits(0);
  double_distance_.set_range(1, 20);
  double_distance_.set_increments(1, 2);
  double_distance_.set_digits(0);
  drag_.set_range(1, 32);
  drag_.set_increments(1, 4);
  drag_.set_digits(0);
  FormGrid behavior_form;
  behavior_form.add("Double-click time (ms)", double_time_);
  behavior_form.add("Double-click distance", double_distance_);
  behavior_form.add("Drag threshold", drag_);
  behavior->pack_start(behavior_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Behavior", *behavior), Gtk::PACK_SHRINK);

  auto* theme = make_vbox(8);
  cursor_size_.set_range(0, 128);
  cursor_size_.set_increments(1, 8);
  cursor_size_.set_digits(0);
  FormGrid theme_form;
  theme_form.add("Cursor theme", cursor_theme_);
  theme_form.add("Cursor size (0 = default)", cursor_size_);
  theme->pack_start(theme_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Cursor", *theme), Gtk::PACK_SHRINK);

  device_.signal_changed().connect([this] {
    if (freeze_ == 0) {
      show_device();
    }
  });
  enabled_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      write_bool_atom(kDeviceEnabled, enabled_.get_active());
    }
  });
  right_handed_.signal_toggled().connect([this] {
    if (freeze_ > 0 || current() == nullptr) {
      return;
    }
    store_.set_bool("pointers", pointer_prop(current()->xfconf_name, "RightHanded"),
                    right_handed_.get_active());
  });
  reverse_.signal_toggled().connect([this] {
    if (freeze_ > 0 || current() == nullptr) {
      return;
    }
    store_.set_bool("pointers", pointer_prop(current()->xfconf_name, "ReverseScrolling"),
                    reverse_.get_active());
  });
  acceleration_.signal_value_changed().connect([this] {
    if (freeze_ > 0 || current() == nullptr) {
      return;
    }
    store_.set_double("pointers", pointer_prop(current()->xfconf_name, "Acceleration"),
                      acceleration_.get_value());
  });
  threshold_.signal_value_changed().connect([this] {
    if (freeze_ > 0 || current() == nullptr) {
      return;
    }
    store_.set_int("pointers", pointer_prop(current()->xfconf_name, "Threshold"),
                   static_cast<int>(threshold_.get_value()));
  });
  tap_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      write_bool_atom(kTap, tap_.get_active());
    }
  });
  natural_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      write_bool_atom(kNatural, natural_.get_active());
    }
  });
  disable_typing_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      write_bool_atom(kDisableTyping, disable_typing_.get_active());
    }
  });
  middle_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      write_bool_atom(kMiddle, middle_.get_active());
    }
  });
  scroll_method_.signal_changed().connect([this] {
    if (freeze_ == 0) {
      write_scroll_method();
    }
  });
  click_method_.signal_changed().connect([this] {
    if (freeze_ > 0 || current() == nullptr) {
      return;
    }
    int areas = 0;
    int fingers = 0;
    const std::string id = click_method_.get_active_id();
    if (id == "button-areas") {
      areas = 1;
    } else if (id == "clickfinger") {
      fingers = 1;
    }
    store_.set_ints("pointers", pointer_xinput_prop(current()->xfconf_name, kClickMethod),
                    {areas, fingers});
  });
  disable_typing_global_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      store_.set_bool("pointers", "/DisableTouchpadWhileTyping",
                      disable_typing_global_.get_active());
    }
    typing_duration_.set_sensitive(disable_typing_global_.get_active());
  });
  typing_duration_.signal_value_changed().connect([this] {
    if (freeze_ == 0) {
      store_.set_double("pointers", "/DisableTouchpadDuration", typing_duration_.get_value());
    }
  });
  double_time_.signal_value_changed().connect([this] {
    if (freeze_ == 0) {
      store_.set_int("xsettings", "/Net/DoubleClickTime", double_time_.get_value_as_int());
    }
  });
  double_distance_.signal_value_changed().connect([this] {
    if (freeze_ == 0) {
      store_.set_int("xsettings", "/Net/DoubleClickDistance", double_distance_.get_value_as_int());
    }
  });
  drag_.signal_value_changed().connect([this] {
    if (freeze_ == 0) {
      store_.set_int("xsettings", "/Net/DndDragThreshold", drag_.get_value_as_int());
    }
  });
  cursor_theme_.signal_changed().connect([this] {
    if (freeze_ == 0 && !cursor_theme_.get_active_id().empty()) {
      store_.set_string("xsettings", "/Gtk/CursorThemeName", cursor_theme_.get_active_id());
    }
  });
  cursor_size_.signal_value_changed().connect([this] {
    if (freeze_ == 0) {
      store_.set_int("xsettings", "/Gtk/CursorThemeSize", cursor_size_.get_value_as_int());
    }
  });

  reload_devices();
  {
    Freeze freeze(freeze_);
    double_time_.set_value(store_.get_int("xsettings", "/Net/DoubleClickTime", 400));
    double_distance_.set_value(store_.get_int("xsettings", "/Net/DoubleClickDistance", 5));
    drag_.set_value(store_.get_int("xsettings", "/Net/DndDragThreshold", 8));
    disable_typing_global_.set_active(
        store_.get_bool("pointers", "/DisableTouchpadWhileTyping", false));
    typing_duration_.set_value(store_.get_double("pointers", "/DisableTouchpadDuration", 2.0));
    typing_duration_.set_sensitive(disable_typing_global_.get_active());
    const auto themes = scan_cursor_themes(data_roots("icons", true));
    const std::string current = store_.get_string("xsettings", "/Gtk/CursorThemeName", "");
    bool saw = current.empty();
    for (const auto& theme : themes) {
      cursor_theme_.append(theme.id, theme.name);
      if (theme.id == current) {
        saw = true;
      }
    }
    if (!current.empty() && !saw) {
      cursor_theme_.append(current, current);
    }
    if (!current.empty()) {
      cursor_theme_.set_active_id(current);
    } else if (!themes.empty()) {
      cursor_theme_.set_active(0);
    }
    cursor_size_.set_value(store_.get_int("xsettings", "/Gtk/CursorThemeSize", 0));
  }
  show_all();
}

void MousePage::reload_devices() {
  devices_ = pointers_.list();
  Freeze freeze(freeze_);
  device_.remove_all();
  for (const auto& item : devices_) {
    device_.append(item.xfconf_name, item.name);
  }
  if (!devices_.empty()) {
    device_.set_active(0);
  }
  show_device();
}

const PointerDevice* MousePage::current() const {
  const int index = device_.get_active_row_number();
  if (index < 0 || index >= static_cast<int>(devices_.size())) {
    return nullptr;
  }
  return &devices_[static_cast<std::size_t>(index)];
}

void MousePage::show_device() {
  Freeze freeze(freeze_);
  const PointerDevice* device = current();
  const bool have = device != nullptr;
  enabled_.set_sensitive(have && device->has_atom(kDeviceEnabled));
  right_handed_.set_sensitive(have);
  left_handed_.set_sensitive(have);
  acceleration_.set_sensitive(have);
  threshold_.set_sensitive(have);
  reverse_.set_sensitive(have);
  const bool touch = have && device->touchpad;
  tap_.set_sensitive(touch || (have && device->has_atom(kTap)));
  natural_.set_sensitive(touch || (have && device->has_atom(kNatural)));
  disable_typing_.set_sensitive(touch || (have && device->has_atom(kDisableTyping)));
  scroll_method_.set_sensitive(touch || (have && device->has_atom(kScrollMethod)));
  click_method_.set_sensitive(touch || (have && device->has_atom(kClickMethod)));
  middle_.set_sensitive(touch || (have && device->has_atom(kMiddle)));
  if (!have) {
    device_note_->set_text("No pointer devices were found.");
    return;
  }
  device_note_->set_text(device->touchpad ? "Touchpad options below are stored for this device."
                                          : "This device is not a touchpad. Touchpad options stay off.");
  const std::string id = device->xfconf_name;
  if (device->has_atom(kDeviceEnabled)) {
    const int on = store_.get_int("pointers", pointer_xinput_prop(id, kDeviceEnabled), 1);
    enabled_.set_active(on != 0);
  } else {
    enabled_.set_active(true);
  }
  const bool right = store_.get_bool("pointers", pointer_prop(id, "RightHanded"), true);
  right_handed_.set_active(right);
  left_handed_.set_active(!right);
  acceleration_.set_value(store_.get_double("pointers", pointer_prop(id, "Acceleration"), 5.0));
  threshold_.set_value(store_.get_int("pointers", pointer_prop(id, "Threshold"), 4));
  reverse_.set_active(store_.get_bool("pointers", pointer_prop(id, "ReverseScrolling"), false));
  tap_.set_active(store_.get_int("pointers", pointer_xinput_prop(id, kTap), 0) != 0);
  natural_.set_active(store_.get_int("pointers", pointer_xinput_prop(id, kNatural), 0) != 0);
  disable_typing_.set_active(
      store_.get_int("pointers", pointer_xinput_prop(id, kDisableTyping), 0) != 0);
  middle_.set_active(store_.get_int("pointers", pointer_xinput_prop(id, kMiddle), 0) != 0);
  const auto scroll = store_.get_ints("pointers", pointer_xinput_prop(id, kScrollMethod));
  std::string scroll_id = "two-finger";
  if (scroll.size() >= 2 && scroll[1] != 0) {
    scroll_id = "edge";
  } else if (scroll.size() >= 3 && scroll[2] != 0) {
    scroll_id = "button";
  } else if (scroll.size() >= 1 && scroll[0] == 0 && (scroll.size() < 2 || scroll[1] == 0)) {
    scroll_id = scroll.empty() ? "two-finger" : "none";
  }
  scroll_method_.set_active_id(scroll_id);
  const auto click = store_.get_ints("pointers", pointer_xinput_prop(id, kClickMethod));
  std::string click_id = "button-areas";
  if (click.size() >= 2 && click[1] != 0) {
    click_id = "clickfinger";
  } else if (click.size() >= 1 && click[0] == 0) {
    click_id = "none";
  }
  click_method_.set_active_id(click_id);
}

void MousePage::write_bool_atom(const char* atom, bool value) {
  if (current() == nullptr) {
    return;
  }
  store_.set_int("pointers", pointer_xinput_prop(current()->xfconf_name, atom), value ? 1 : 0);
}

void MousePage::write_scroll_method() {
  if (current() == nullptr) {
    return;
  }
  int two = 0;
  int edge = 0;
  int button = 0;
  const std::string id = scroll_method_.get_active_id();
  if (id == "two-finger") {
    two = 1;
  } else if (id == "edge") {
    edge = 1;
  } else if (id == "button") {
    button = 1;
  }
  store_.set_ints("pointers", pointer_xinput_prop(current()->xfconf_name, kScrollMethod),
                  {two, edge, button});
}

}  // namespace lcos
