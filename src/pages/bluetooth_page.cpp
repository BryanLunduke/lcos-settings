// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/bluetooth_page.hpp"

#include "logic/sample.hpp"
#include "ui_util.hpp"

#include <glibmm/main.h>
#include <gtkmm/image.h>
#include <gtkmm/scrolledwindow.h>

namespace lcos {
namespace {

Gtk::ListBoxRow* device_row(const BtDevice& device) {
  auto* row = Gtk::manage(new Gtk::ListBoxRow());
  auto* line = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
  line->set_margin_top(4);
  line->set_margin_bottom(4);
  line->set_margin_start(8);
  line->set_margin_end(8);
  auto* icon = Gtk::manage(new Gtk::Image());
  icon->set_from_icon_name(bluetooth_device_icon(device.icon), Gtk::ICON_SIZE_LARGE_TOOLBAR);
  auto* text = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 1));
  auto* name = make_label(device.name.empty() ? device.address : device.name);
  name->get_style_context()->add_class("lcos-strong");
  name->set_ellipsize(Pango::ELLIPSIZE_END);
  auto* detail = make_label(bluetooth_device_status(device));
  detail->get_style_context()->add_class("lcos-row-sub");
  detail->set_ellipsize(Pango::ELLIPSIZE_END);
  text->pack_start(*name, Gtk::PACK_SHRINK);
  text->pack_start(*detail, Gtk::PACK_SHRINK);
  line->pack_start(*icon, Gtk::PACK_SHRINK);
  line->pack_start(*text, Gtk::PACK_EXPAND_WIDGET);
  row->add(*line);
  row->set_name(device.path);
  return row;
}

}  // namespace

BluetoothPage::BluetoothPage() : trusted_("_Trusted", true), scan_("_Scan", true) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  auto* page = make_vbox(8);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  page->pack_start(*make_empty_state("bluetooth", "Looking for Bluetooth…", &status_), Gtk::PACK_SHRINK);
  page->set_margin_bottom(16);
  body_ = make_vbox(8);

  auto* adapter = make_vbox(8);
  FormGrid form;
  form.add("Powered", powered_);
  form.add("Discoverable", discoverable_);
  form.add("Name", name_);
  adapter->pack_start(form.grid(), Gtk::PACK_SHRINK);
  body_->pack_start(*make_frame("Adapter", *adapter), Gtk::PACK_SHRINK);

  devices_.set_selection_mode(Gtk::SELECTION_SINGLE);
  auto* list_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  list_scroll->get_style_context()->add_class("lcos-list");
  list_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  list_scroll->set_shadow_type(Gtk::SHADOW_IN);
  list_scroll->set_size_request(-1, 160);
  list_scroll->set_propagate_natural_height(false);
  list_scroll->add(devices_);
  auto* devices = make_vbox(8);
  devices->pack_start(*list_scroll, Gtk::PACK_SHRINK);
  devices->pack_start(trusted_, Gtk::PACK_SHRINK);
  auto* actions = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  auto* pair = Gtk::manage(new Gtk::Button("_Pair", true));
  auto* connect = Gtk::manage(new Gtk::Button("_Connect", true));
  auto* disconnect = Gtk::manage(new Gtk::Button("_Disconnect", true));
  auto* remove = Gtk::manage(new Gtk::Button("_Remove", true));
  auto* advanced = Gtk::manage(new Gtk::Button("Ad_vanced…", true));
  actions->pack_start(scan_, Gtk::PACK_SHRINK);
  actions->pack_start(*pair, Gtk::PACK_SHRINK);
  actions->pack_start(*connect, Gtk::PACK_SHRINK);
  actions->pack_start(*disconnect, Gtk::PACK_SHRINK);
  actions->pack_start(*remove, Gtk::PACK_SHRINK);
  actions->pack_end(*advanced, Gtk::PACK_SHRINK);
  devices->pack_start(*actions, Gtk::PACK_SHRINK);
  body_->pack_start(*make_frame("Devices", *devices), Gtk::PACK_SHRINK);
  page->pack_start(*body_, Gtk::PACK_SHRINK);

  powered_.property_active().signal_changed().connect([this] {
    if (freeze_ > 0 || !snapshot_.adapter) {
      return;
    }
    remember_adapter();
    const std::string error = client_.set_powered(snapshot_.adapter_info.path, powered_.get_active());
    if (!error.empty()) {
      tell(error);
      refresh();
      return;
    }
    dirty_ = true;
    pending_changed.emit();
  });
  discoverable_.property_active().signal_changed().connect([this] {
    if (freeze_ > 0 || !snapshot_.adapter) {
      return;
    }
    remember_adapter();
    const std::string error =
        client_.set_discoverable(snapshot_.adapter_info.path, discoverable_.get_active());
    if (!error.empty()) {
      tell(error);
      refresh();
      return;
    }
    dirty_ = true;
    pending_changed.emit();
  });
  name_.signal_activate().connect([this] {
    if (freeze_ > 0 || !snapshot_.adapter) {
      return;
    }
    remember_adapter();
    const std::string error = client_.set_name(snapshot_.adapter_info.path, name_.get_text());
    if (!error.empty()) {
      tell(error);
      return;
    }
    dirty_ = true;
    pending_changed.emit();
    refresh();
  });
  name_.signal_focus_out_event().connect([this](GdkEventFocus*) {
    if (freeze_ == 0 && snapshot_.adapter && name_.get_text() != snapshot_.adapter_info.name) {
      remember_adapter();
      const std::string error = client_.set_name(snapshot_.adapter_info.path, name_.get_text());
      if (!error.empty()) {
        tell(error);
      } else {
        dirty_ = true;
        pending_changed.emit();
      }
    }
    return false;
  });
  devices_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
    if (freeze_ > 0) {
      return;
    }
    selected_ = row != nullptr ? std::string(row->get_name()) : std::string();
    const BtDevice* device = selected();
    Freeze freeze(freeze_);
    trusted_.set_sensitive(device != nullptr);
    trusted_.set_active(device != nullptr && device->trusted);
  });
  trusted_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    const BtDevice* device = selected();
    if (device == nullptr) {
      return;
    }
    if (original_trust_.count(device->path) == 0) {
      original_trust_[device->path] = device->trusted;
    }
    const std::string error = client_.set_trusted(device->path, trusted_.get_active());
    if (!error.empty()) {
      tell(error);
      refresh();
      return;
    }
    dirty_ = true;
    pending_changed.emit();
  });
  scan_.signal_clicked().connect([this] {
    if (!snapshot_.adapter) {
      return;
    }
    const std::string error = client_.start_discovery(snapshot_.adapter_info.path);
    if (!error.empty()) {
      tell(error);
      return;
    }
    scans_left_ = 8;
    scan_.set_sensitive(false);
    if (!scan_timer_.connected()) {
      scan_timer_ = Glib::signal_timeout().connect(sigc::mem_fun(*this, &BluetoothPage::scan_tick), 2000);
    }
  });
  pair->signal_clicked().connect([this] {
    const BtDevice* device = selected();
    if (device == nullptr) {
      return;
    }
    const std::string error = client_.pair_device(device->path);
    if (!error.empty()) {
      tell(error);
    }
    refresh();
  });
  connect->signal_clicked().connect([this] {
    const BtDevice* device = selected();
    if (device == nullptr) {
      return;
    }
    const std::string error = client_.connect_device(device->path);
    if (!error.empty()) {
      tell(error);
    }
    refresh();
  });
  disconnect->signal_clicked().connect([this] {
    const BtDevice* device = selected();
    if (device == nullptr) {
      return;
    }
    const std::string error = client_.disconnect_device(device->path);
    if (!error.empty()) {
      tell(error);
    }
    refresh();
  });
  remove->signal_clicked().connect([this] {
    const BtDevice* device = selected();
    if (device == nullptr || !snapshot_.adapter) {
      return;
    }
    const std::string name = device->name.empty() ? device->address : device->name;
    const std::string path = device->path;
    if (!confirm_action(*this, "Remove " + name + "?", "This forgets the device.", "_Remove")) {
      return;
    }
    const std::string error = client_.remove_device(snapshot_.adapter_info.path, path);
    if (!error.empty()) {
      tell(error);
      return;
    }
    original_trust_.erase(path);
    if (selected_ == path) {
      selected_.clear();
    }
    refresh();
  });
  advanced->signal_clicked().connect([this] { launch_program(*this, "blueman-manager"); });
  show_all();
  refresh();
}

BluetoothPage::~BluetoothPage() {
  scan_timer_.disconnect();
  if (snapshot_.adapter && scans_left_ > 0) {
    client_.stop_discovery(snapshot_.adapter_info.path);
  }
}

void BluetoothPage::on_page_shown() { refresh(); }

void BluetoothPage::tell(const std::string& message) { report_error(*this, "Bluetooth", message); }

void BluetoothPage::remember_adapter() {
  if (have_adapter_ || !snapshot_.adapter) {
    return;
  }
  original_.powered = snapshot_.adapter_info.powered;
  original_.discoverable = snapshot_.adapter_info.discoverable;
  original_.name = snapshot_.adapter_info.name;
  have_adapter_ = true;
}

const BtDevice* BluetoothPage::selected() const {
  for (const auto& device : snapshot_.devices) {
    if (device.path == selected_) {
      return &device;
    }
  }
  return nullptr;
}

bool BluetoothPage::scan_tick() {
  refresh();
  if (--scans_left_ <= 0) {
    if (snapshot_.adapter) {
      client_.stop_discovery(snapshot_.adapter_info.path);
    }
    scan_.set_sensitive(snapshot_.adapter);
    return false;
  }
  return true;
}

void BluetoothPage::refresh() {
  const bool sample = settings_sample();
  snapshot_ = client_.snapshot(sample);
  auto show_banner = [this](bool show) {
    if (auto* banner = status_->get_parent()) {
      if (show) {
        banner->show();
      } else {
        banner->hide();
      }
    }
  };
  if (!snapshot_.service) {
    status_->set_text(snapshot_.error.empty() ? "Bluetooth is not available." : snapshot_.error);
    show_banner(true);
    body_->set_sensitive(false);
  } else if (!snapshot_.adapter) {
    status_->set_text("No Bluetooth adapter is available.");
    show_banner(true);
    body_->set_sensitive(false);
  } else {
    status_->set_text("");
    show_banner(false);
    body_->set_sensitive(true);
  }
  rebuild();
}

void BluetoothPage::rebuild() {
  Freeze freeze(freeze_);
  powered_.set_active(snapshot_.adapter_info.powered);
  discoverable_.set_active(snapshot_.adapter_info.discoverable);
  if (!name_.has_focus()) {
    name_.set_text(snapshot_.adapter_info.name);
  }
  for (auto* child : devices_.get_children()) {
    devices_.remove(*child);
  }
  for (const auto& device : snapshot_.devices) {
    devices_.append(*device_row(device));
  }
  if (snapshot_.devices.empty()) {
    auto* row = Gtk::manage(new Gtk::ListBoxRow());
    row->set_selectable(false);
    auto* label = make_note(snapshot_.adapter ? "No devices yet. Scan to look for some."
                                              : "Devices appear when an adapter is available.");
    label->set_margin_top(8);
    label->set_margin_bottom(8);
    row->add(*label);
    devices_.append(*row);
  }
  devices_.show_all();
  bool found = false;
  for (auto* child : devices_.get_children()) {
    if (child->get_name() == selected_) {
      devices_.select_row(*static_cast<Gtk::ListBoxRow*>(child));
      found = true;
    }
  }
  const BtDevice* device = found ? selected() : nullptr;
  trusted_.set_sensitive(device != nullptr && snapshot_.adapter);
  trusted_.set_active(device != nullptr && device->trusted);
  scan_.set_sensitive(snapshot_.adapter && scans_left_ <= 0);
}

bool BluetoothPage::commit_pending() {
  dirty_ = false;
  have_adapter_ = false;
  original_trust_.clear();
  pending_changed.emit();
  return true;
}

void BluetoothPage::revert_page() {
  if (!dirty_) {
    return;
  }
  if (have_adapter_ && snapshot_.adapter) {
    client_.set_powered(snapshot_.adapter_info.path, original_.powered);
    client_.set_discoverable(snapshot_.adapter_info.path, original_.discoverable);
    if (!original_.name.empty()) {
      client_.set_name(snapshot_.adapter_info.path, original_.name);
    }
  }
  for (const auto& item : original_trust_) {
    client_.set_trusted(item.first, item.second);
  }
  dirty_ = false;
  have_adapter_ = false;
  original_trust_.clear();
  pending_changed.emit();
}

bool BluetoothPage::discard_pending() {
  if (!dirty_) {
    return false;
  }
  revert_page();
  refresh();
  return true;
}

std::string BluetoothPage::pending_detail() const {
  return dirty_ ? "Bluetooth changes have not been kept." : std::string();
}

}  // namespace lcos
