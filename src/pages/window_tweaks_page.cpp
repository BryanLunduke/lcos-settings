// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/window_tweaks_page.hpp"

#include "logic/wm_shortcuts.hpp"
#include "ui_util.hpp"

#include <gtkmm/box.h>
#include <gtkmm/scrolledwindow.h>

namespace lcos {
namespace {

constexpr const char* kChannel = kWmChannel;

void prepare_opacity(Gtk::Scale& scale) {
  scale.set_range(0, 100);
  scale.set_increments(1, 10);
  scale.set_digits(0);
  scale.set_value_pos(Gtk::POS_RIGHT);
  scale.set_hexpand(true);
}

}  // namespace

WindowTweaksPage::WindowTweaksPage(XfconfStore& store)
    : store_(store),
      activate_bring_("Bring the window on the current workspace"),
      activate_switch_("Switch to the window's workspace"),
      activate_none_("Do nothing"),
      placement_ratio_(Gtk::ORIENTATION_HORIZONTAL),
      place_center_("At the center of the screen"),
      place_mouse_("Under the mouse pointer"),
      frame_opacity_(Gtk::ORIENTATION_HORIZONTAL),
      inactive_opacity_(Gtk::ORIENTATION_HORIZONTAL),
      move_opacity_(Gtk::ORIENTATION_HORIZONTAL),
      resize_opacity_(Gtk::ORIENTATION_HORIZONTAL),
      popup_opacity_(Gtk::ORIENTATION_HORIZONTAL) {
  activate_switch_.join_group(activate_bring_);
  activate_none_.join_group(activate_bring_);
  place_mouse_.join_group(place_center_);
  placement_ratio_.set_range(0, 100);
  placement_ratio_.set_increments(1, 10);
  placement_ratio_.set_digits(0);
  placement_ratio_.set_value_pos(Gtk::POS_RIGHT);
  for (Gtk::Scale* scale :
       {&frame_opacity_, &inactive_opacity_, &move_opacity_, &resize_opacity_, &popup_opacity_}) {
    prepare_opacity(*scale);
  }

  cycle_minimum_.set_label("Skip windows that have \"skip pager\" or \"skip taskbar\" set");
  cycle_minimized_.set_label("Cycle through minimized windows in most recently used order");
  cycle_hidden_.set_label("Include hidden (iconified) windows");
  cycle_draw_frame_.set_label("Draw a frame around the selected window while cycling");
  cycle_raise_.set_label("Raise windows while cycling");
  cycle_list_.set_label("Cycle through windows in a list");
  prevent_stealing_.set_label("Activate focus stealing prevention");
  focus_hint_.set_label("Honor the standard ICCCM focus hint");
  raise_any_.set_label("Raise windows when any mouse button is pressed");
  borderless_.set_label("Hide the frame of windows when maximized");
  titleless_.set_label("Hide the title of windows when maximized");
  tile_on_move_.set_label("Automatically tile windows when moving toward the screen edge");
  snap_resist_.set_label("Use edge resistance instead of window snapping");
  rollup_.set_label("Use the mouse wheel on the title bar to roll up the window");
  urgent_blink_.set_label("Notify of urgency by making the window decoration blink");
  urgent_repeat_.set_label("Keep urgent windows blinking repeatedly");
  compositing_.set_label("Enable display compositing");
  unredirect_.set_label("Display fullscreen overlay windows directly");
  cycle_preview_.set_label("Show window previews in place of icons when cycling");
  popup_shadow_.set_label("Show shadows under popup windows");
  dock_shadow_.set_label("Show shadows under dock windows");
  frame_shadow_.set_label("Show shadows under regular windows");
  zoom_desktop_.set_label("Zoom the desktop with the mouse wheel");
  zoom_pointer_.set_label("Zoom the pointer along with the desktop");

  const char* modifiers[] = {"None", "Alt", "Control", "Hyper", "Meta", "Shift",
                             "Super", "Mod1", "Mod2", "Mod3", "Mod4", "Mod5"};
  for (const char* modifier : modifiers) {
    easy_click_.append(modifier, modifier);
  }

  pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);

  auto* cycling = make_vbox(8);
  cycling->set_border_width(12);
  for (Gtk::CheckButton* check : {&cycle_minimum_, &cycle_minimized_, &cycle_hidden_,
                                  &cycle_draw_frame_, &cycle_raise_, &cycle_list_}) {
    cycling->pack_start(*check, Gtk::PACK_SHRINK);
  }
  notebook_.append_page(*make_page_scroll(*cycling), "Cycling");

  auto* focus = make_vbox(8);
  focus->set_border_width(12);
  focus->pack_start(prevent_stealing_, Gtk::PACK_SHRINK);
  focus->pack_start(focus_hint_, Gtk::PACK_SHRINK);
  auto* raised = make_vbox(4);
  raised->pack_start(*make_label("When a window raises itself"), Gtk::PACK_SHRINK);
  raised->pack_start(activate_bring_, Gtk::PACK_SHRINK);
  raised->pack_start(activate_switch_, Gtk::PACK_SHRINK);
  raised->pack_start(activate_none_, Gtk::PACK_SHRINK);
  focus->pack_start(*make_frame("Raised windows", *raised), Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*focus), "Focus");

  auto* access = make_vbox(8);
  access->set_border_width(12);
  FormGrid grab(240);
  grab.add("Key used to grab and move windows", easy_click_);
  access->pack_start(grab.grid(), Gtk::PACK_SHRINK);
  for (Gtk::CheckButton* check : {&raise_any_, &borderless_, &titleless_, &tile_on_move_,
                                  &snap_resist_, &rollup_, &urgent_blink_, &urgent_repeat_}) {
    access->pack_start(*check, Gtk::PACK_SHRINK);
  }
  notebook_.append_page(*make_page_scroll(*access), "Accessibility");

  auto* placement = make_vbox(8);
  placement->set_border_width(12);
  FormGrid ratio(260);
  ratio.add("Minimum size for smart placement", placement_ratio_);
  placement->pack_start(ratio.grid(), Gtk::PACK_SHRINK);
  auto* where = make_vbox(4);
  where->pack_start(*make_label("By default, place windows"), Gtk::PACK_SHRINK);
  where->pack_start(place_center_, Gtk::PACK_SHRINK);
  where->pack_start(place_mouse_, Gtk::PACK_SHRINK);
  placement->pack_start(*where, Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*placement), "Placement");

  auto* compositor = make_vbox(8);
  compositor->set_border_width(12);
  compositor->pack_start(compositing_, Gtk::PACK_SHRINK);
  compositor_options_ = make_vbox(8);
  for (Gtk::CheckButton* check : {&unredirect_, &cycle_preview_, &popup_shadow_, &dock_shadow_,
                                  &frame_shadow_, &zoom_desktop_, &zoom_pointer_}) {
    compositor_options_->pack_start(*check, Gtk::PACK_SHRINK);
  }
  FormGrid opacity(240);
  opacity.add("Opacity of window decorations", frame_opacity_);
  opacity.add("Opacity of inactive windows", inactive_opacity_);
  opacity.add("Opacity of windows during move", move_opacity_);
  opacity.add("Opacity of windows during resize", resize_opacity_);
  opacity.add("Opacity of popup windows", popup_opacity_);
  compositor_options_->pack_start(opacity.grid(), Gtk::PACK_SHRINK);
  compositor->pack_start(*compositor_options_, Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*compositor), "Compositor");

  auto bind_bool = [this](Gtk::CheckButton& check, const char* prop) {
    check.signal_toggled().connect([this, &check, prop] {
      if (freeze_ > 0) {
        return;
      }
      store_.set_bool(kChannel, prop, check.get_active());
    });
  };
  bind_bool(cycle_minimum_, "/general/cycle_minimum");
  bind_bool(cycle_minimized_, "/general/cycle_minimized");
  bind_bool(cycle_hidden_, "/general/cycle_hidden");
  bind_bool(cycle_draw_frame_, "/general/cycle_draw_frame");
  bind_bool(cycle_raise_, "/general/cycle_raise");
  cycle_list_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_int(kChannel, "/general/cycle_tabwin_mode", cycle_list_.get_active() ? 1 : 0);
  });
  bind_bool(prevent_stealing_, "/general/prevent_focus_stealing");
  bind_bool(focus_hint_, "/general/focus_hint");
  activate_bring_.signal_toggled().connect([this] {
    if (freeze_ > 0 || !activate_bring_.get_active()) {
      return;
    }
    store_.set_string(kChannel, "/general/activate_action", "bring");
  });
  activate_switch_.signal_toggled().connect([this] {
    if (freeze_ > 0 || !activate_switch_.get_active()) {
      return;
    }
    store_.set_string(kChannel, "/general/activate_action", "switch");
  });
  activate_none_.signal_toggled().connect([this] {
    if (freeze_ > 0 || !activate_none_.get_active()) {
      return;
    }
    store_.set_string(kChannel, "/general/activate_action", "none");
  });
  easy_click_.signal_changed().connect([this] {
    if (freeze_ > 0 || easy_click_.get_active_id().empty()) {
      return;
    }
    store_.set_string(kChannel, "/general/easy_click", easy_click_.get_active_id());
  });
  bind_bool(raise_any_, "/general/raise_with_any_button");
  borderless_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/general/borderless_maximize", borderless_.get_active());
    if (!borderless_.get_active()) {
      titleless_.set_active(false);
    }
    sync_maximize();
  });
  bind_bool(titleless_, "/general/titleless_maximize");
  tile_on_move_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/general/tile_on_move", tile_on_move_.get_active());
    if (tile_on_move_.get_active()) {
      store_.set_bool(kChannel, "/general/wrap_windows", false);
    }
  });
  bind_bool(snap_resist_, "/general/snap_resist");
  bind_bool(rollup_, "/general/mousewheel_rollup");
  urgent_blink_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/general/urgent_blink", urgent_blink_.get_active());
    if (!urgent_blink_.get_active()) {
      urgent_repeat_.set_active(false);
    }
    sync_urgent();
  });
  bind_bool(urgent_repeat_, "/general/repeat_urgent_blink");
  placement_ratio_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_int(kChannel, "/general/placement_ratio",
                   static_cast<int>(placement_ratio_.get_value()));
  });
  place_center_.signal_toggled().connect([this] {
    if (freeze_ > 0 || !place_center_.get_active()) {
      return;
    }
    store_.set_string(kChannel, "/general/placement_mode", "center");
  });
  place_mouse_.signal_toggled().connect([this] {
    if (freeze_ > 0 || !place_mouse_.get_active()) {
      return;
    }
    store_.set_string(kChannel, "/general/placement_mode", "mouse");
  });
  compositing_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_bool(kChannel, "/general/use_compositing", compositing_.get_active());
    sync_compositor();
  });
  bind_bool(unredirect_, "/general/unredirect_overlays");
  bind_bool(cycle_preview_, "/general/cycle_preview");
  bind_bool(popup_shadow_, "/general/show_popup_shadow");
  bind_bool(dock_shadow_, "/general/show_dock_shadow");
  bind_bool(frame_shadow_, "/general/show_frame_shadow");
  bind_bool(zoom_desktop_, "/general/zoom_desktop");
  bind_bool(zoom_pointer_, "/general/zoom_pointer");
  auto bind_opacity = [this](Gtk::Scale& scale, const char* prop) {
    scale.signal_value_changed().connect([this, &scale, prop] {
      if (freeze_ > 0) {
        return;
      }
      store_.set_int(kChannel, prop, static_cast<int>(scale.get_value()));
    });
  };
  bind_opacity(frame_opacity_, "/general/frame_opacity");
  bind_opacity(inactive_opacity_, "/general/inactive_opacity");
  bind_opacity(move_opacity_, "/general/move_opacity");
  bind_opacity(resize_opacity_, "/general/resize_opacity");
  bind_opacity(popup_opacity_, "/general/popup_opacity");

  load();
}

void WindowTweaksPage::sync_compositor() {
  if (compositor_options_ != nullptr) {
    compositor_options_->set_sensitive(compositing_.get_active());
  }
}

void WindowTweaksPage::sync_maximize() { titleless_.set_sensitive(borderless_.get_active()); }

void WindowTweaksPage::sync_urgent() { urgent_repeat_.set_sensitive(urgent_blink_.get_active()); }

void WindowTweaksPage::on_page_shown() { load(); }

void WindowTweaksPage::load() {
  Freeze freeze(freeze_);
  cycle_minimum_.set_active(store_.get_bool(kChannel, "/general/cycle_minimum", false));
  cycle_minimized_.set_active(store_.get_bool(kChannel, "/general/cycle_minimized", false));
  cycle_hidden_.set_active(store_.get_bool(kChannel, "/general/cycle_hidden", true));
  cycle_draw_frame_.set_active(store_.get_bool(kChannel, "/general/cycle_draw_frame", true));
  cycle_raise_.set_active(store_.get_bool(kChannel, "/general/cycle_raise", false));
  cycle_list_.set_active(store_.get_int(kChannel, "/general/cycle_tabwin_mode", 0) != 0);
  prevent_stealing_.set_active(store_.get_bool(kChannel, "/general/prevent_focus_stealing", false));
  focus_hint_.set_active(store_.get_bool(kChannel, "/general/focus_hint", true));
  const std::string action = store_.get_string(kChannel, "/general/activate_action", "bring");
  if (action == "switch") {
    activate_switch_.set_active(true);
  } else if (action == "none") {
    activate_none_.set_active(true);
  } else {
    activate_bring_.set_active(true);
  }
  std::string modifier = store_.get_string(kChannel, "/general/easy_click", "Alt");
  if (modifier == "true") {
    modifier = "Alt";
  }
  easy_click_.set_active_id(modifier);
  if (easy_click_.get_active_row_number() < 0) {
    easy_click_.set_active_id("Alt");
  }
  raise_any_.set_active(store_.get_bool(kChannel, "/general/raise_with_any_button", false));
  borderless_.set_active(store_.get_bool(kChannel, "/general/borderless_maximize", false));
  titleless_.set_active(store_.get_bool(kChannel, "/general/titleless_maximize", false));
  tile_on_move_.set_active(store_.get_bool(kChannel, "/general/tile_on_move", false));
  snap_resist_.set_active(store_.get_bool(kChannel, "/general/snap_resist", false));
  rollup_.set_active(store_.get_bool(kChannel, "/general/mousewheel_rollup", false));
  urgent_blink_.set_active(store_.get_bool(kChannel, "/general/urgent_blink", false));
  urgent_repeat_.set_active(store_.get_bool(kChannel, "/general/repeat_urgent_blink", false));
  placement_ratio_.set_value(store_.get_int(kChannel, "/general/placement_ratio", 20));
  const std::string placement = store_.get_string(kChannel, "/general/placement_mode", "center");
  if (placement == "mouse") {
    place_mouse_.set_active(true);
  } else {
    place_center_.set_active(true);
  }
  compositing_.set_active(store_.get_bool(kChannel, "/general/use_compositing", true));
  unredirect_.set_active(store_.get_bool(kChannel, "/general/unredirect_overlays", false));
  cycle_preview_.set_active(store_.get_bool(kChannel, "/general/cycle_preview", true));
  popup_shadow_.set_active(store_.get_bool(kChannel, "/general/show_popup_shadow", false));
  dock_shadow_.set_active(store_.get_bool(kChannel, "/general/show_dock_shadow", false));
  frame_shadow_.set_active(store_.get_bool(kChannel, "/general/show_frame_shadow", false));
  zoom_desktop_.set_active(store_.get_bool(kChannel, "/general/zoom_desktop", false));
  zoom_pointer_.set_active(store_.get_bool(kChannel, "/general/zoom_pointer", false));
  frame_opacity_.set_value(store_.get_int(kChannel, "/general/frame_opacity", 100));
  inactive_opacity_.set_value(store_.get_int(kChannel, "/general/inactive_opacity", 100));
  move_opacity_.set_value(store_.get_int(kChannel, "/general/move_opacity", 100));
  resize_opacity_.set_value(store_.get_int(kChannel, "/general/resize_opacity", 100));
  popup_opacity_.set_value(store_.get_int(kChannel, "/general/popup_opacity", 100));
  sync_compositor();
  sync_maximize();
  sync_urgent();
}

void WindowTweaksPage::prepare_screenshot(int index) {
  if (index >= 0 && index < notebook_.get_n_pages()) {
    notebook_.set_current_page(index);
  }
}

std::string WindowTweaksPage::screenshot_suffix(int index) const {
  switch (index) {
    case 0:
      return "cycling";
    case 1:
      return "focus";
    case 2:
      return "accessibility";
    case 3:
      return "placement";
    case 4:
      return "compositor";
    default:
      return {};
  }
}

}  // namespace lcos
