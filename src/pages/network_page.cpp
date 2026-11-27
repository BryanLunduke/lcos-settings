// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/network_page.hpp"

#include "logic/sample.hpp"
#include "ui_util.hpp"

#include <gtkmm/filechooserdialog.h>
#include <gtkmm/scrolledwindow.h>

#include <fstream>

namespace lcos {
namespace {

Gtk::ListBoxRow* connection_row(const NetConnection& connection) {
  auto* row = Gtk::manage(new Gtk::ListBoxRow());
  auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 1));
  box->set_margin_top(4);
  box->set_margin_bottom(4);
  box->set_margin_start(8);
  box->set_margin_end(8);
  auto* name = make_label(connection.name);
  name->get_style_context()->add_class("lcos-strong");
  name->set_ellipsize(Pango::ELLIPSIZE_END);
  std::string detail = connection_type_label(connection.type);
  detail += connection.autoconnect ? " · Connect automatically" : " · Manual connect";
  auto* line = make_label(detail);
  line->get_style_context()->add_class("lcos-row-sub");
  line->set_ellipsize(Pango::ELLIPSIZE_END);
  box->pack_start(*name, Gtk::PACK_SHRINK);
  box->pack_start(*line, Gtk::PACK_SHRINK);
  row->add(*box);
  row->set_name(connection.uuid);
  return row;
}

}  // namespace

NetworkPage::NetworkPage()
    : autoconnect_("Connect _automatically", true),
      prefix_(Gtk::Adjustment::create(24, 0, 32, 1, 4)),
      show_password_("_Show password", true) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  auto* page = make_vbox(8);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  status_ = make_note("Looking for NetworkManager…");
  page->pack_start(*status_, Gtk::PACK_SHRINK);
  wired_ = make_label("Wired: —");
  wifi_ = make_label("Wi-Fi: —");
  wired_->set_ellipsize(Pango::ELLIPSIZE_END);
  wifi_->set_ellipsize(Pango::ELLIPSIZE_END);
  auto* links = make_vbox(4);
  links->pack_start(*wired_, Gtk::PACK_SHRINK);
  links->pack_start(*wifi_, Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Status", *links), Gtk::PACK_SHRINK);

  list_.set_selection_mode(Gtk::SELECTION_SINGLE);
  auto* list_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  list_scroll->get_style_context()->add_class("lcos-list");
  list_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  list_scroll->set_shadow_type(Gtk::SHADOW_IN);
  list_scroll->set_size_request(-1, 120);
  list_scroll->set_propagate_natural_height(false);
  list_scroll->add(list_);

  method_.append("auto", "Automatic (DHCP)");
  method_.append("manual", "Manual");
  name_.set_hexpand(true);
  address_.set_hexpand(true);
  gateway_.set_hexpand(true);
  dns_.set_hexpand(true);
  password_.set_hexpand(true);
  password_.set_visibility(false);
  prefix_.set_numeric(true);
  prefix_.set_digits(0);

  editor_ = make_vbox(8);
  FormGrid form;
  form.add("Name", name_);
  form.add_control(autoconnect_);
  form.add("IPv4", method_);
  manual_ = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
  FormGrid manual_form;
  manual_form.add("Address", address_);
  manual_form.add("Prefix", prefix_);
  manual_form.add("Gateway", gateway_);
  manual_form.add("DNS", dns_);
  manual_->add(manual_form.grid());
  form.add_full(*manual_);
  wifi_box_ = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
  FormGrid wifi_form;
  wifi_form.add("Password", password_);
  wifi_form.add_control(show_password_);
  wifi_box_->add(wifi_form.grid());
  form.add_full(*wifi_box_);
  editor_->pack_start(form.grid(), Gtk::PACK_SHRINK);
  auto* buttons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  auto* save = Gtk::manage(new Gtk::Button("_Save", true));
  auto* forget = Gtk::manage(new Gtk::Button("_Forget", true));
  auto* import_vpn = Gtk::manage(new Gtk::Button("Import _VPN…", true));
  auto* advanced = Gtk::manage(new Gtk::Button("Ad_vanced…", true));
  buttons->pack_start(*save, Gtk::PACK_SHRINK);
  buttons->pack_start(*forget, Gtk::PACK_SHRINK);
  buttons->pack_start(*import_vpn, Gtk::PACK_SHRINK);
  buttons->pack_end(*advanced, Gtk::PACK_SHRINK);
  editor_->pack_start(*buttons, Gtk::PACK_SHRINK);
  plugin_note_ = make_note("VPN import uses the NetworkManager OpenVPN and WireGuard plugins when they are installed.");
  editor_->pack_start(*plugin_note_, Gtk::PACK_SHRINK);
  editor_->pack_start(
      *make_note("DNS here belongs to the selected connection. Command Center does not change system DNS."),
      Gtk::PACK_SHRINK);

  auto* saved = make_vbox(8);
  saved->pack_start(*list_scroll, Gtk::PACK_SHRINK);
  saved->pack_start(*editor_, Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Saved connections", *saved), Gtk::PACK_SHRINK);

  list_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
    if (freeze_ > 0) {
      return;
    }
    selected_ = row != nullptr ? std::string(row->get_name()) : std::string();
    form_dirty_ = false;
    fill_form(selected());
  });
  auto edited = [this] { mark_form(); };
  name_.signal_changed().connect(edited);
  address_.signal_changed().connect(edited);
  gateway_.signal_changed().connect(edited);
  dns_.signal_changed().connect(edited);
  password_.signal_changed().connect(edited);
  prefix_.signal_value_changed().connect(edited);
  method_.signal_changed().connect([this] {
    manual_->set_visible(method_.get_active_id() == "manual");
    mark_form();
  });
  autoconnect_.signal_toggled().connect(edited);
  show_password_.signal_toggled().connect([this] {
    password_.set_visibility(show_password_.get_active());
  });
  save->signal_clicked().connect([this] { save_form(); });
  forget->signal_clicked().connect([this] {
    const NetConnection* connection = selected();
    if (connection == nullptr) {
      return;
    }
    if (!confirm_action(*this, "Forget " + connection->name + "?",
                        "This removes the saved connection.", "_Forget")) {
      return;
    }
    const std::string error = client_.forget(connection->uuid);
    if (!error.empty()) {
      tell(error);
      return;
    }
    baseline_.erase(connection->uuid);
    if (selected_ == connection->uuid) {
      selected_.clear();
      form_uuid_.clear();
      form_dirty_ = false;
    }
    pending_changed.emit();
    refresh(false);
  });
  import_vpn->signal_clicked().connect([this] {
    Gtk::FileChooserDialog dialog("Import VPN", Gtk::FILE_CHOOSER_ACTION_OPEN);
    if (auto* parent = dynamic_cast<Gtk::Window*>(get_toplevel())) {
      dialog.set_transient_for(*parent);
    }
    dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("_Import", Gtk::RESPONSE_ACCEPT);
    auto filter = Gtk::FileFilter::create();
    filter->set_name("OpenVPN and WireGuard");
    filter->add_pattern("*.ovpn");
    filter->add_pattern("*.conf");
    dialog.add_filter(filter);
    if (dialog.run() != Gtk::RESPONSE_ACCEPT) {
      return;
    }
    const std::string path = dialog.get_filename();
    dialog.hide();
    std::ifstream input(path);
    if (!input) {
      tell("That file could not be read.");
      return;
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const VpnFile file = parse_vpn_file(text, path);
    if (file.kind == VpnKind::OpenVpn && !snapshot_.plugins.openvpn) {
      tell("The NetworkManager OpenVPN plugin is not installed.");
      return;
    }
    if (file.kind == VpnKind::WireGuard && !snapshot_.plugins.wireguard) {
      tell("NetworkManager's WireGuard support is not installed.");
      return;
    }
    const std::string error = client_.import_vpn(file, vpn_connection_name(path));
    if (!error.empty()) {
      tell(error);
      return;
    }
    refresh(false);
  });
  advanced->signal_clicked().connect([this] { launch_program(*this, "nm-connection-editor"); });
  show_all();
  manual_->hide();
  refresh(false);
}

void NetworkPage::on_page_shown() { refresh(form_dirty_); }

void NetworkPage::tell(const std::string& message) { report_error(*this, "Network", message); }

const NetConnection* NetworkPage::selected() const {
  for (const auto& connection : snapshot_.connections) {
    if (connection.uuid == selected_) {
      return &connection;
    }
  }
  return nullptr;
}

void NetworkPage::mark_form() {
  if (freeze_ > 0 || form_uuid_.empty()) {
    return;
  }
  if (!form_dirty_) {
    form_dirty_ = true;
    pending_changed.emit();
  }
}

NetConnection NetworkPage::edited_connection() const {
  NetConnection edited;
  if (const NetConnection* current = selected()) {
    edited = *current;
  }
  edited.uuid = form_uuid_;
  edited.name = name_.get_text();
  edited.autoconnect = autoconnect_.get_active();
  const std::string method = method_.get_active_id();
  edited.ipv4_method = method.empty() ? edited.ipv4_method : std::string(method);
  edited.addresses.clear();
  edited.gateway.clear();
  edited.dns.clear();
  if (edited.ipv4_method == "manual") {
    NetAddress address;
    std::string host = address_.get_text();
    int prefix = static_cast<int>(prefix_.get_value());
    if (host.find('/') != std::string::npos) {
      ipv4_cidr_ok(host, &host, &prefix);
    }
    address.address = host;
    address.prefix = prefix;
    edited.addresses.push_back(address);
    edited.gateway = gateway_.get_text();
    edited.dns = split_dns_list(dns_.get_text());
  }
  return edited;
}

bool NetworkPage::save_form() {
  if (form_uuid_.empty()) {
    return true;
  }
  const NetConnection edited = edited_connection();
  if (edited.name.empty()) {
    tell("The connection needs a name.");
    return false;
  }
  Ipv4Edit check;
  check.method = edited.ipv4_method;
  if (!edited.addresses.empty()) {
    check.address = edited.addresses.front().address;
    check.prefix = edited.addresses.front().prefix;
  }
  check.gateway = edited.gateway;
  check.dns = format_dns_list(edited.dns);
  const std::string invalid = ipv4_edit_error(check);
  if (!invalid.empty()) {
    tell(invalid);
    return false;
  }
  const bool set_password = edited.wifi && password_.get_text_length() > 0;
  if (baseline_.count(edited.uuid) == 0) {
    if (const NetConnection* current = selected()) {
      Baseline baseline;
      baseline.connection = *current;
      if (set_password) {
        std::string secret;
        if (client_.wifi_password(edited.uuid, secret).empty()) {
          baseline.password = true;
          baseline.secret = secret;
        }
      }
      baseline_[edited.uuid] = baseline;
    }
  }
  const std::string error = client_.save(edited, password_.get_text(), set_password);
  if (!error.empty()) {
    tell(error);
    return false;
  }
  form_dirty_ = false;
  pending_changed.emit();
  refresh(false);
  return true;
}

void NetworkPage::fill_form(const NetConnection* connection) {
  Freeze freeze(freeze_);
  const bool have = connection != nullptr;
  editor_->set_sensitive(have && snapshot_.available);
  if (!have) {
    form_uuid_.clear();
    name_.set_text("");
    password_.set_text("");
    manual_->hide();
    wifi_box_->hide();
    return;
  }
  form_uuid_ = connection->uuid;
  name_.set_text(connection->name);
  autoconnect_.set_active(connection->autoconnect);
  const std::string method = connection->ipv4_method.empty() ? "auto" : connection->ipv4_method;
  method_.set_active_id(method);
  if (method_.get_active_id().empty()) {
    method_.append(method, method);
    method_.set_active_id(method);
  }
  manual_->set_visible(method_.get_active_id() == "manual");
  if (!connection->addresses.empty()) {
    address_.set_text(connection->addresses.front().address);
    prefix_.set_value(connection->addresses.front().prefix);
  } else {
    address_.set_text("");
    prefix_.set_value(24);
  }
  gateway_.set_text(connection->gateway);
  dns_.set_text(format_dns_list(connection->dns));
  wifi_box_->set_visible(connection->wifi);
  password_.set_text("");
  show_password_.set_active(false);
  password_.set_visibility(false);
  if (connection->wifi && snapshot_.available && !settings_sample()) {
    std::string secret;
    if (client_.wifi_password(connection->uuid, secret).empty()) {
      password_.set_text(secret);
    }
  } else if (connection->wifi && settings_sample()) {
    password_.set_text("sample-secret");
  }
}

void NetworkPage::refresh(bool keep_form) {
  snapshot_ = client_.snapshot(settings_sample());
  if (!snapshot_.available) {
    status_->set_text(snapshot_.error.empty() ? "NetworkManager is not running." : snapshot_.error);
    wired_->set_text("Wired: unavailable");
    wifi_->set_text("Wi-Fi: unavailable");
    editor_->set_sensitive(false);
  } else {
    status_->set_text("");
    status_->hide();
    bool saw_wired = false;
    bool saw_wifi = false;
    for (const auto& link : snapshot_.links) {
      if (link.kind == "wifi" && !saw_wifi) {
        wifi_->set_text(link_status_text(link));
        saw_wifi = true;
      } else if (link.kind != "wifi" && !saw_wired) {
        wired_->set_text(link_status_text(link));
        saw_wired = true;
      }
    }
    if (!saw_wired) {
      wired_->set_text("Wired: no device");
    }
    if (!saw_wifi) {
      wifi_->set_text("Wi-Fi: no device");
    }
    editor_->set_sensitive(true);
  }
  std::string plugins;
  if (snapshot_.available && !snapshot_.plugins.openvpn) {
    plugins = "OpenVPN import needs the NetworkManager OpenVPN plugin. ";
  }
  if (snapshot_.available && !snapshot_.plugins.wireguard) {
    plugins += "WireGuard import needs NetworkManager's WireGuard support.";
  }
  if (plugins.empty()) {
    plugin_note_->set_text(
        "VPN import uses the NetworkManager OpenVPN and WireGuard plugins when they are installed.");
  } else {
    plugin_note_->set_text(plugins);
  }
  Freeze freeze(freeze_);
  for (auto* child : list_.get_children()) {
    list_.remove(*child);
  }
  for (const auto& connection : snapshot_.connections) {
    list_.append(*connection_row(connection));
  }
  list_.show_all();
  bool found = false;
  for (auto* child : list_.get_children()) {
    if (child->get_name() == selected_) {
      list_.select_row(*static_cast<Gtk::ListBoxRow*>(child));
      found = true;
    }
  }
  if (!found) {
    if (!snapshot_.connections.empty()) {
      selected_ = snapshot_.connections.front().uuid;
      if (auto* row = list_.get_row_at_index(0)) {
        list_.select_row(*row);
      }
    } else {
      selected_.clear();
    }
  }
  if (!keep_form) {
    form_dirty_ = false;
    fill_form(selected());
  }
}

bool NetworkPage::commit_pending() {
  if (form_dirty_ && !save_form()) {
    return false;
  }
  baseline_.clear();
  form_dirty_ = false;
  pending_changed.emit();
  return true;
}

void NetworkPage::revert_page() {
  for (const auto& item : baseline_) {
    client_.save(item.second.connection, item.second.secret, item.second.password);
  }
  baseline_.clear();
  form_dirty_ = false;
  pending_changed.emit();
}

bool NetworkPage::discard_pending() {
  if (!has_pending()) {
    return false;
  }
  revert_page();
  refresh(false);
  return true;
}

std::string NetworkPage::pending_detail() const {
  if (!has_pending()) {
    return {};
  }
  return form_dirty_ ? "This connection has not been saved." : "Network changes have not been kept.";
}

}  // namespace lcos
