// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/keyboard_page.hpp"

#include "logic/shortcuts.hpp"
#include "logic/text.hpp"
#include "ui_util.hpp"

#include <gdk/gdkkeysyms.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/label.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/scrolledwindow.h>

#include <fstream>
#include <map>

namespace lcos {
namespace {

class GrabDialog : public Gtk::Dialog {
 public:
  explicit GrabDialog(Gtk::Window& parent) : Gtk::Dialog("Shortcut", parent, true) {
    auto* label = Gtk::manage(
        new Gtk::Label("Press the key combination. Escape cancels."));
    label->set_margin_top(16);
    label->set_margin_bottom(16);
    label->set_margin_start(16);
    label->set_margin_end(16);
    get_content_area()->pack_start(*label, Gtk::PACK_SHRINK);
    add_button("Cancel", Gtk::RESPONSE_CANCEL);
    set_default_size(380, 120);
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

bool ask_command(Gtk::Window& parent, std::string& command, bool& startup) {
  Gtk::Dialog dialog("Application shortcut", parent, true);
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_margin_top(8);
  grid->set_margin_bottom(8);
  grid->set_margin_start(8);
  grid->set_margin_end(8);
  auto* entry = Gtk::manage(new Gtk::Entry());
  entry->set_text(command);
  entry->set_hexpand(true);
  auto* browse = Gtk::manage(new Gtk::Button("Browse…"));
  auto* notify = Gtk::manage(new Gtk::CheckButton("Startup notification"));
  notify->set_active(startup);
  grid->attach(*make_label("Command"), 0, 0, 1, 1);
  grid->attach(*entry, 1, 0, 1, 1);
  grid->attach(*browse, 2, 0, 1, 1);
  grid->attach(*notify, 1, 1, 2, 1);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("OK", Gtk::RESPONSE_OK);
  dialog.show_all();
  browse->signal_clicked().connect([&] {
    Gtk::FileChooserDialog chooser(dialog, "Command", Gtk::FILE_CHOOSER_ACTION_OPEN);
    chooser.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    chooser.add_button("Open", Gtk::RESPONSE_OK);
    if (chooser.run() == Gtk::RESPONSE_OK) {
      entry->set_text(chooser.get_filename());
    }
  });
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return false;
  }
  command = trim_copy(entry->get_text());
  startup = notify->get_active();
  return !command.empty();
}

}  // namespace

KeyboardPage::KeyboardPage(XfconfStore& store, Gtk::Window& parent)
    : store_(store),
      parent_(parent),
      delay_(Gtk::ORIENTATION_HORIZONTAL),
      rate_(Gtk::ORIENTATION_HORIZONTAL) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  auto* page = make_vbox(4);
  page->set_border_width(0);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  auto* behavior = make_vbox(8);
  repeat_.set_label("Key repeat");
  delay_.set_range(10, 1000);
  delay_.set_increments(10, 50);
  delay_.set_value(500);
  delay_.set_digits(0);
  rate_.set_range(1, 250);
  rate_.set_increments(1, 10);
  rate_.set_value(20);
  rate_.set_digits(0);
  blink_.set_label("Cursor blinks");
  blink_time_.set_range(100, 2500);
  blink_time_.set_increments(50, 100);
  blink_time_.set_digits(0);
  numlock_.set_label("Restore Num Lock at login");
  FormGrid behavior_form;
  behavior_form.add_full(repeat_);
  behavior_form.add("Delay (ms)", delay_);
  behavior_form.add("Speed (per second)", rate_);
  behavior_form.add_full(blink_);
  behavior_form.add("Blink time (ms)", blink_time_);
  behavior_form.add_full(numlock_);
  auto* test = Gtk::manage(new Gtk::Entry());
  test->set_placeholder_text("Type here to try key repeat");
  behavior_form.add("Test", *test);
  behavior->pack_start(behavior_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Behavior", *behavior), Gtk::PACK_SHRINK);

  auto* layout = make_vbox(8);
  system_defaults_.set_label("Use system defaults");
  layout->pack_start(system_defaults_, Gtk::PACK_SHRINK);
  FormGrid model_form;
  model_form.add("Model", model_);
  layout->pack_start(model_form.grid(), Gtk::PACK_SHRINK);
  layouts_ = Gtk::ListStore::create(layout_columns_);
  layouts_view_.set_model(layouts_);
  layouts_view_.append_column("Layout", layout_columns_.layout);
  layouts_view_.append_column("Variant", layout_columns_.variant);
  layouts_view_.set_headers_visible(true);
  layouts_view_.get_selection()->set_mode(Gtk::SELECTION_SINGLE);
  auto* layout_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*layout_scroll);
  layout_scroll->set_size_request(-1, -1);
  layout_scroll->add(layouts_view_);
  layout->pack_start(*layout_scroll, Gtk::PACK_SHRINK);
  auto* layout_buttons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  auto* add_layout = Gtk::manage(new Gtk::Button("Add"));
  auto* remove_layout = Gtk::manage(new Gtk::Button("Remove"));
  auto* up = Gtk::manage(new Gtk::Button("Up"));
  auto* down = Gtk::manage(new Gtk::Button("Down"));
  layout_buttons->pack_start(*add_layout, Gtk::PACK_SHRINK);
  layout_buttons->pack_start(*remove_layout, Gtk::PACK_SHRINK);
  layout_buttons->pack_start(*up, Gtk::PACK_SHRINK);
  layout_buttons->pack_start(*down, Gtk::PACK_SHRINK);
  layout->pack_start(*layout_buttons, Gtk::PACK_SHRINK);
  FormGrid option_form;
  option_form.add("Variant", variant_);
  option_form.add("Switch layout", group_);
  option_form.add("Compose key", compose_);
  layout->pack_start(option_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Layout", *layout), Gtk::PACK_SHRINK);

  auto* shortcuts = make_vbox(8);
  shortcuts_ = Gtk::ListStore::create(shortcut_columns_);
  shortcuts_view_.set_model(shortcuts_);
  shortcuts_view_.append_column("Shortcut", shortcut_columns_.accel);
  shortcuts_view_.append_column("Command", shortcut_columns_.command);
  shortcuts_view_.append_column("From", shortcut_columns_.origin);
  auto* shortcut_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*shortcut_scroll);
  shortcut_scroll->set_size_request(-1, -1);
  shortcut_scroll->add(shortcuts_view_);
  shortcuts->pack_start(*shortcut_scroll, Gtk::PACK_SHRINK);
  auto* shortcut_buttons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  auto* add_shortcut = Gtk::manage(new Gtk::Button("Add"));
  auto* edit_shortcut = Gtk::manage(new Gtk::Button("Edit"));
  auto* remove_shortcut = Gtk::manage(new Gtk::Button("Remove"));
  shortcut_buttons->pack_start(*add_shortcut, Gtk::PACK_SHRINK);
  shortcut_buttons->pack_start(*edit_shortcut, Gtk::PACK_SHRINK);
  shortcut_buttons->pack_start(*remove_shortcut, Gtk::PACK_SHRINK);
  shortcuts->pack_start(*shortcut_buttons, Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Application shortcuts", *shortcuts), Gtk::PACK_SHRINK);

  repeat_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      store_.set_bool("keyboards", "/Default/KeyRepeat", repeat_.get_active());
    }
    delay_.set_sensitive(repeat_.get_active());
    rate_.set_sensitive(repeat_.get_active());
  });
  delay_.signal_value_changed().connect([this] {
    if (freeze_ == 0) {
      store_.set_int("keyboards", "/Default/KeyRepeat/Delay",
                     static_cast<int>(delay_.get_value()));
    }
  });
  rate_.signal_value_changed().connect([this] {
    if (freeze_ == 0) {
      store_.set_int("keyboards", "/Default/KeyRepeat/Rate",
                     static_cast<int>(rate_.get_value()));
    }
  });
  blink_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      store_.set_bool("xsettings", "/Net/CursorBlink", blink_.get_active());
    }
    blink_time_.set_sensitive(blink_.get_active());
  });
  blink_time_.signal_value_changed().connect([this] {
    if (freeze_ == 0) {
      store_.set_int("xsettings", "/Net/CursorBlinkTime", blink_time_.get_value_as_int());
    }
  });
  numlock_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      store_.set_bool("keyboards", "/Default/RestoreNumlock", numlock_.get_active());
    }
  });
  system_defaults_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      store_.set_bool("keyboard-layout", "/Default/XkbDisable", system_defaults_.get_active());
      if (!system_defaults_.get_active()) {
        write_layouts();
      }
    }
    set_layout_controls_sensitive();
  });
  model_.signal_changed().connect([this] {
    if (freeze_ == 0 && !model_.get_active_id().empty()) {
      store_.set_string("keyboard-layout", "/Default/XkbModel", model_.get_active_id());
    }
  });
  variant_.signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    auto iter = layouts_view_.get_selection()->get_selected();
    if (!iter) {
      return;
    }
    (*iter)[layout_columns_.variant_id] = variant_.get_active_id();
    const std::string layout_id =
        static_cast<Glib::ustring>((*iter)[layout_columns_.layout_id]);
    const std::string variant_id = variant_.get_active_id();
    (*iter)[layout_columns_.variant_id] = variant_id;
    (*iter)[layout_columns_.variant] = variant_description(layout_id, variant_id);
    write_layouts();
  });
  group_.signal_changed().connect([this] {
    if (freeze_ == 0) {
      store_.set_string("keyboard-layout", "/Default/XkbOptions/Group", group_.get_active_id());
    }
  });
  compose_.signal_changed().connect([this] {
    if (freeze_ == 0) {
      store_.set_string("keyboard-layout", "/Default/XkbOptions/Compose", compose_.get_active_id());
    }
  });
  layouts_view_.get_selection()->signal_changed().connect(
      sigc::mem_fun(*this, &KeyboardPage::refill_variant_combo));
  add_layout->signal_clicked().connect([this] {
    Gtk::Dialog dialog("Add layout", parent_, true);
    Gtk::ComboBoxText choices;
    for (const auto& item : rules_.layouts) {
      choices.append(item.name, item.description);
    }
    choices.set_margin_top(8);
    choices.set_margin_bottom(8);
    choices.set_margin_start(8);
    choices.set_margin_end(8);
    if (!rules_.layouts.empty()) {
      choices.set_active(0);
    }
    dialog.get_content_area()->pack_start(choices, Gtk::PACK_SHRINK);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Add", Gtk::RESPONSE_OK);
    dialog.show_all();
    if (dialog.run() != Gtk::RESPONSE_OK || choices.get_active_id().empty()) {
      return;
    }
    auto row = layouts_->append();
    (*row)[layout_columns_.layout_id] = choices.get_active_id();
    (*row)[layout_columns_.layout] = layout_description(choices.get_active_id());
    (*row)[layout_columns_.variant_id] = "";
    (*row)[layout_columns_.variant] = "Default";
    layouts_view_.get_selection()->select(row);
    write_layouts();
  });
  remove_layout->signal_clicked().connect([this] {
    auto iter = layouts_view_.get_selection()->get_selected();
    if (!iter) {
      return;
    }
    layouts_->erase(iter);
    write_layouts();
  });
  auto move_row = [this](int direction) {
    auto iter = layouts_view_.get_selection()->get_selected();
    if (!iter) {
      return;
    }
    if (direction < 0) {
      Gtk::TreeIter previous = iter;
      if (previous != layouts_->children().begin()) {
        --previous;
        layouts_->iter_swap(iter, previous);
        write_layouts();
      }
    } else {
      Gtk::TreeIter next = iter;
      ++next;
      if (next) {
        layouts_->iter_swap(iter, next);
        write_layouts();
      }
    }
  };
  up->signal_clicked().connect([move_row] { move_row(-1); });
  down->signal_clicked().connect([move_row] { move_row(1); });
  add_shortcut->signal_clicked().connect(sigc::mem_fun(*this, &KeyboardPage::add_shortcut));
  edit_shortcut->signal_clicked().connect(sigc::mem_fun(*this, &KeyboardPage::edit_shortcut));
  remove_shortcut->signal_clicked().connect(sigc::mem_fun(*this, &KeyboardPage::remove_shortcut));

  load_rules();
  load_behavior();
  load_layouts();
  load_shortcuts();
  show_all();
}

void KeyboardPage::load_rules() {
  const char* paths[] = {"/usr/share/X11/xkb/rules/evdev.xml",
                         "/usr/share/X11/xkb/rules/base.xml"};
  std::string xml;
  for (const char* path : paths) {
    std::ifstream in(path);
    if (!in) {
      continue;
    }
    xml.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (!xml.empty()) {
      break;
    }
  }
  std::string error;
  if (!xml.empty()) {
    parse_xkb_rules(xml, rules_, error);
  }
  Freeze freeze(freeze_);
  model_.remove_all();
  for (const auto& item : rules_.models) {
    model_.append(item.name, item.description);
  }
  group_.remove_all();
  group_.append("", "None");
  compose_.remove_all();
  compose_.append("", "None");
  if (const XkbOptionGroup* grp = find_option_group(rules_, "grp")) {
    for (const auto& option : grp->options) {
      group_.append(option.name, option.description);
    }
  }
  if (const XkbOptionGroup* compose = find_option_group(rules_, "compose")) {
    for (const auto& option : compose->options) {
      compose_.append(option.name, option.description);
    }
  }
}

void KeyboardPage::load_behavior() {
  Freeze freeze(freeze_);
  repeat_.set_active(store_.get_bool("keyboards", "/Default/KeyRepeat", true));
  delay_.set_value(store_.get_int("keyboards", "/Default/KeyRepeat/Delay", 500));
  rate_.set_value(store_.get_int("keyboards", "/Default/KeyRepeat/Rate", 20));
  delay_.set_sensitive(repeat_.get_active());
  rate_.set_sensitive(repeat_.get_active());
  blink_.set_active(store_.get_bool("xsettings", "/Net/CursorBlink", true));
  blink_time_.set_value(store_.get_int("xsettings", "/Net/CursorBlinkTime", 1200));
  blink_time_.set_sensitive(blink_.get_active());
  numlock_.set_active(store_.get_bool("keyboards", "/Default/RestoreNumlock", true));
}

void KeyboardPage::set_layout_controls_sensitive() {
  const bool enabled = !system_defaults_.get_active();
  model_.set_sensitive(enabled);
  layouts_view_.set_sensitive(enabled);
  variant_.set_sensitive(enabled);
  group_.set_sensitive(enabled);
  compose_.set_sensitive(enabled);
}

std::string KeyboardPage::layout_description(const std::string& id) const {
  if (const XkbLayout* layout = find_layout(rules_, id)) {
    return layout->description;
  }
  return id;
}

std::string KeyboardPage::variant_description(const std::string& layout_id,
                                             const std::string& variant_id) const {
  if (variant_id.empty()) {
    return "Default";
  }
  if (const XkbLayout* layout = find_layout(rules_, layout_id)) {
    for (const auto& variant : layout->variants) {
      if (variant.name == variant_id) {
        return variant.description;
      }
    }
  }
  return variant_id;
}

void KeyboardPage::load_layouts() {
  Freeze freeze(freeze_);
  system_defaults_.set_active(store_.get_bool("keyboard-layout", "/Default/XkbDisable", true));
  const std::string model = store_.get_string("keyboard-layout", "/Default/XkbModel", "pc105");
  model_.set_active_id(model);
  if (model_.get_active_id().empty() && !model.empty()) {
    model_.append(model, model);
    model_.set_active_id(model);
  }
  layouts_->clear();
  const auto choices = decode_layouts(
      store_.get_string("keyboard-layout", "/Default/XkbLayout", ""),
      store_.get_string("keyboard-layout", "/Default/XkbVariant", ""));
  for (const auto& choice : choices) {
    auto row = layouts_->append();
    (*row)[layout_columns_.layout_id] = choice.layout;
    (*row)[layout_columns_.layout] = layout_description(choice.layout);
    (*row)[layout_columns_.variant_id] = choice.variant;
    (*row)[layout_columns_.variant] = variant_description(choice.layout, choice.variant);
  }
  if (!layouts_->children().empty()) {
    layouts_view_.get_selection()->select(layouts_->children().begin());
  }
  group_.set_active_id(store_.get_string("keyboard-layout", "/Default/XkbOptions/Group", ""));
  compose_.set_active_id(store_.get_string("keyboard-layout", "/Default/XkbOptions/Compose", ""));
  set_layout_controls_sensitive();
  refill_variant_combo();
}

void KeyboardPage::write_layouts() {
  std::vector<LayoutChoice> choices;
  for (auto row : layouts_->children()) {
    LayoutChoice choice;
    choice.layout = static_cast<Glib::ustring>((*row)[layout_columns_.layout_id]);
    choice.variant = static_cast<Glib::ustring>((*row)[layout_columns_.variant_id]);
    if (!choice.layout.empty()) {
      choices.push_back(std::move(choice));
    }
  }
  const auto encoded = encode_layouts(choices);
  store_.set_string("keyboard-layout", "/Default/XkbLayout", encoded.first);
  store_.set_string("keyboard-layout", "/Default/XkbVariant", encoded.second);
}

void KeyboardPage::refill_variant_combo() {
  Freeze freeze(freeze_);
  variant_.remove_all();
  variant_.append("", "Default");
  auto iter = layouts_view_.get_selection()->get_selected();
  if (!iter) {
    variant_.set_active_id("");
    return;
  }
  const std::string layout_id = static_cast<Glib::ustring>((*iter)[layout_columns_.layout_id]);
  if (const XkbLayout* layout = find_layout(rules_, layout_id)) {
    for (const auto& item : layout->variants) {
      variant_.append(item.name, item.description);
    }
  }
  const std::string current = static_cast<Glib::ustring>((*iter)[layout_columns_.variant_id]);
  variant_.set_active_id(current);
  if (variant_.get_active_id().empty()) {
    variant_.set_active(0);
  }
}

void KeyboardPage::load_shortcuts() {
  std::map<std::string, std::string> values;
  for (const char* base : {kShortcutCustomBase, kShortcutDefaultBase}) {
    for (const auto& entry : store_.list(kShortcutsChannel, base)) {
      if (entry.second.type == PropType::String) {
        values[entry.first] = entry.second.s;
      } else if (entry.second.type == PropType::Bool) {
        values[entry.first] = entry.second.b ? "true" : "false";
      }
    }
  }
  shortcuts_->clear();
  for (const auto& shortcut : merge_shortcuts(values)) {
    auto row = shortcuts_->append();
    (*row)[shortcut_columns_.accel] = shortcut.accel;
    (*row)[shortcut_columns_.command] = shortcut.command;
    (*row)[shortcut_columns_.startup] = shortcut.startup_notify;
    (*row)[shortcut_columns_.custom] = shortcut.custom;
    (*row)[shortcut_columns_.origin] = shortcut.custom ? "Custom" : "Default";
  }
}

void KeyboardPage::add_shortcut() {
  std::string command;
  bool startup = true;
  if (!ask_command(parent_, command, startup)) {
    return;
  }
  GrabDialog grab(parent_);
  if (grab.run() != Gtk::RESPONSE_OK || grab.accel.empty()) {
    return;
  }
  for (auto row : shortcuts_->children()) {
    const std::string existing = static_cast<Glib::ustring>((*row)[shortcut_columns_.accel]);
    if (existing == grab.accel) {
      Gtk::MessageDialog dialog(parent_, "Replace the existing shortcut?", false,
                                Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO, true);
      dialog.set_secondary_text(existing + " currently runs " +
                                static_cast<Glib::ustring>((*row)[shortcut_columns_.command]));
      if (dialog.run() != Gtk::RESPONSE_YES) {
        return;
      }
      break;
    }
  }
  store_.set_string(kShortcutsChannel, shortcut_command_path(true, grab.accel), command);
  store_.set_bool(kShortcutsChannel, shortcut_notify_path(true, grab.accel), startup);
  load_shortcuts();
}

void KeyboardPage::edit_shortcut() {
  auto iter = shortcuts_view_.get_selection()->get_selected();
  if (!iter) {
    return;
  }
  std::string command = static_cast<Glib::ustring>((*iter)[shortcut_columns_.command]);
  bool startup = (*iter)[shortcut_columns_.startup];
  const std::string previous = static_cast<Glib::ustring>((*iter)[shortcut_columns_.accel]);
  if (!ask_command(parent_, command, startup)) {
    return;
  }
  Gtk::MessageDialog change(parent_, "Change the key combination too?", false,
                            Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO, true);
  std::string accel = previous;
  if (change.run() == Gtk::RESPONSE_YES) {
    GrabDialog grab(parent_);
    if (grab.run() != Gtk::RESPONSE_OK || grab.accel.empty()) {
      return;
    }
    accel = grab.accel;
  }
  if (accel != previous) {
    store_.reset(kShortcutsChannel, shortcut_command_path(true, previous), true);
  }
  store_.set_string(kShortcutsChannel, shortcut_command_path(true, accel), command);
  store_.set_bool(kShortcutsChannel, shortcut_notify_path(true, accel), startup);
  load_shortcuts();
}

void KeyboardPage::remove_shortcut() {
  auto iter = shortcuts_view_.get_selection()->get_selected();
  if (!iter) {
    return;
  }
  const bool custom = (*iter)[shortcut_columns_.custom];
  const std::string accel = static_cast<Glib::ustring>((*iter)[shortcut_columns_.accel]);
  if (!custom) {
    Gtk::MessageDialog dialog(parent_, "Default shortcuts stay in the list.", false,
                              Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK, true);
    dialog.set_secondary_text(
        "Add a custom shortcut with the same keys to override one, or edit it.");
    dialog.run();
    return;
  }
  store_.reset(kShortcutsChannel, shortcut_command_path(true, accel), true);
  load_shortcuts();
}

}  // namespace lcos
