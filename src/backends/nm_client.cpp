// SPDX-License-Identifier: GPL-3.0-or-later

#include "backends/nm_client.hpp"

#include "logic/atomic_file.hpp"

#include <NetworkManager.h>

#include <gio/gio.h>

#include <cstring>

namespace lcos {
namespace {

std::string take_error(GError* error, const char* fallback) {
  std::string text = fallback;
  if (error != nullptr) {
    if (error->message != nullptr) {
      text = error->message;
    }
    g_error_free(error);
  }
  return text;
}

bool name_is_owned(const char* name) {
  GError* error = nullptr;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
  if (bus == nullptr) {
    if (error != nullptr) {
      g_error_free(error);
    }
    return false;
  }
  GVariant* reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
      g_variant_new("(s)", name), G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 1500, nullptr,
      &error);
  g_object_unref(bus);
  if (reply == nullptr) {
    if (error != nullptr) {
      g_error_free(error);
    }
    return false;
  }
  gboolean owned = FALSE;
  g_variant_get(reply, "(b)", &owned);
  g_variant_unref(reply);
  return owned != FALSE;
}

bool plugin_file_exists(const char* needle) {
  const char* roots[] = {"/usr/lib/NetworkManager", "/usr/lib/x86_64-linux-gnu/NetworkManager",
                         "/usr/lib/aarch64-linux-gnu/NetworkManager"};
  for (const char* root : roots) {
    GDir* directory = g_dir_open(root, 0, nullptr);
    if (directory == nullptr) {
      continue;
    }
    const char* name = nullptr;
    while ((name = g_dir_read_name(directory)) != nullptr) {
      if (std::strstr(name, needle) != nullptr) {
        g_dir_close(directory);
        return true;
      }
      const std::string child = std::string(root) + "/" + name;
      GDir* nested = g_dir_open(child.c_str(), 0, nullptr);
      if (nested == nullptr) {
        continue;
      }
      const char* inner = nullptr;
      while ((inner = g_dir_read_name(nested)) != nullptr) {
        if (std::strstr(inner, needle) != nullptr) {
          g_dir_close(nested);
          g_dir_close(directory);
          return true;
        }
      }
      g_dir_close(nested);
    }
    g_dir_close(directory);
  }
  return false;
}

NetPlugins detect_plugins() {
  NetPlugins plugins;
  GSList* list = nm_vpn_plugin_info_list_load();
  if (nm_vpn_plugin_info_list_find_by_service(list, "org.freedesktop.NetworkManager.openvpn") !=
          nullptr ||
      nm_vpn_plugin_info_list_find_by_name(list, "openvpn") != nullptr) {
    plugins.openvpn = true;
  }
  g_slist_free_full(list, g_object_unref);
  if (!plugins.openvpn) {
    plugins.openvpn = plugin_file_exists("openvpn");
  }
  plugins.wireguard = plugin_file_exists("wireguard");
  return plugins;
}

std::string ssid_text(GBytes* ssid) {
  if (ssid == nullptr) {
    return {};
  }
  gsize length = 0;
  const guint8* data = static_cast<const guint8*>(g_bytes_get_data(ssid, &length));
  char* utf8 = nm_utils_ssid_to_utf8(data, length);
  std::string text = utf8 != nullptr ? utf8 : "";
  g_free(utf8);
  return text;
}

void read_ip4(NMConnection* connection, NetConnection& info) {
  NMSettingIPConfig* ip4 = nm_connection_get_setting_ip4_config(connection);
  if (ip4 == nullptr) {
    info.ipv4_method = "auto";
    return;
  }
  const char* method = nm_setting_ip_config_get_method(ip4);
  info.ipv4_method = method != nullptr ? method : "auto";
  const guint count = nm_setting_ip_config_get_num_addresses(ip4);
  for (guint index = 0; index < count; ++index) {
    NMIPAddress* address = nm_setting_ip_config_get_address(ip4, index);
    NetAddress row;
    const char* text = nm_ip_address_get_address(address);
    row.address = text != nullptr ? text : "";
    row.prefix = static_cast<int>(nm_ip_address_get_prefix(address));
    info.addresses.push_back(std::move(row));
  }
  const char* gateway = nm_setting_ip_config_get_gateway(ip4);
  info.gateway = gateway != nullptr ? gateway : "";
  const guint dns_count = nm_setting_ip_config_get_num_dns(ip4);
  for (guint index = 0; index < dns_count; ++index) {
    const char* dns = nm_setting_ip_config_get_dns(ip4, index);
    if (dns != nullptr) {
      info.dns.emplace_back(dns);
    }
  }
}

NMRemoteConnection* connection_by_uuid(NMClient* client, const std::string& uuid) {
  if (client == nullptr || uuid.empty()) {
    return nullptr;
  }
  return nm_client_get_connection_by_uuid(client, uuid.c_str());
}

NMClient* open_client(std::string* error) {
  if (!name_is_owned("org.freedesktop.NetworkManager")) {
    if (error != nullptr) {
      *error = "NetworkManager is not running.";
    }
    return nullptr;
  }
  GError* gerror = nullptr;
  NMClient* client = nm_client_new(nullptr, &gerror);
  if (client == nullptr || !nm_client_get_nm_running(client)) {
    if (error != nullptr) {
      *error = take_error(gerror, "NetworkManager is not running.");
    } else if (gerror != nullptr) {
      g_error_free(gerror);
    }
    if (client != nullptr) {
      g_object_unref(client);
    }
    return nullptr;
  }
  return client;
}

std::string write_pem(const std::string& stem, const char* leaf, const std::string& pem) {
  if (pem.empty()) {
    return {};
  }
  const char* config = g_get_user_config_dir();
  const std::string root = config != nullptr ? config : "/tmp";
  const std::string directory = root + "/lcos-settings/vpn";
  g_mkdir_with_parents(directory.c_str(), 0700);
  std::string safe;
  for (char character : stem) {
    const unsigned char value = static_cast<unsigned char>(character);
    safe.push_back(g_ascii_isalnum(value) || character == '-' || character == '_' ? character : '_');
  }
  if (safe.empty()) {
    safe = "vpn";
  }
  const std::string path = directory + "/" + safe + "-" + leaf + ".pem";
  ReplaceRequest request;
  request.mode_if_new = 0600;
  std::string error;
  if (!replace_regular_file(path, pem + "\n", request, &error)) {
    return {};
  }
  return path;
}

bool ipv6_text(const std::string& text) { return text.find(':') != std::string::npos; }

struct AddWait {
  GMainLoop* loop = nullptr;
  NMRemoteConnection* remote = nullptr;
  GError* error = nullptr;
};

void add_done(GObject* source, GAsyncResult* result, gpointer user) {
  auto* wait = static_cast<AddWait*>(user);
  wait->remote = nm_client_add_connection_finish(NM_CLIENT(source), result, &wait->error);
  if (wait->loop != nullptr) {
    g_main_loop_quit(wait->loop);
  }
}

gboolean add_timeout(gpointer user) {
  auto* wait = static_cast<AddWait*>(user);
  if (wait->loop != nullptr && g_main_loop_is_running(wait->loop)) {
    g_main_loop_quit(wait->loop);
  }
  return G_SOURCE_REMOVE;
}

std::string add_connection(NMClient* client, NMConnection* connection) {
  AddWait wait;
  wait.loop = g_main_loop_new(nullptr, FALSE);
  nm_client_add_connection_async(client, connection, TRUE, nullptr, add_done, &wait);
  const guint timer = g_timeout_add_seconds(20, add_timeout, &wait);
  g_main_loop_run(wait.loop);
  g_source_remove(timer);
  g_main_loop_unref(wait.loop);
  std::string error;
  if (wait.remote == nullptr) {
    error = take_error(wait.error, "NetworkManager did not save that connection.");
  } else {
    g_object_unref(wait.remote);
  }
  return error;
}

NetSnapshot sample_snapshot() {
  NetSnapshot snapshot;
  snapshot.available = true;
  snapshot.plugins.openvpn = true;
  snapshot.plugins.wireguard = true;
  NetLink wired;
  wired.kind = "wired";
  wired.iface = "eth0";
  wired.state = 100;
  wired.detail = "Office";
  NetLink wifi;
  wifi.kind = "wifi";
  wifi.iface = "wlan0";
  wifi.state = 100;
  wifi.detail = "LCOS";
  snapshot.links.push_back(wired);
  snapshot.links.push_back(wifi);
  NetConnection home;
  home.uuid = "sample-wifi";
  home.name = "LCOS";
  home.type = "802-11-wireless";
  home.wifi = true;
  home.autoconnect = true;
  home.ipv4_method = "auto";
  NetConnection desk;
  desk.uuid = "sample-wired";
  desk.name = "Office";
  desk.type = "802-3-ethernet";
  desk.autoconnect = true;
  desk.ipv4_method = "manual";
  desk.addresses.push_back({"192.168.1.20", 24});
  desk.gateway = "192.168.1.1";
  desk.dns = {"192.168.1.1"};
  snapshot.connections.push_back(home);
  snapshot.connections.push_back(desk);
  return snapshot;
}

}  // namespace

NetSnapshot NmClient::snapshot(bool sample) const {
  if (sample) {
    return sample_snapshot();
  }
  NetSnapshot snapshot;
  std::string error;
  NMClient* client = open_client(&error);
  if (client == nullptr) {
    snapshot.error = error.empty() ? "NetworkManager is not running." : error;
    return snapshot;
  }
  snapshot.available = true;
  snapshot.plugins = detect_plugins();
  const GPtrArray* devices = nm_client_get_devices(client);
  if (devices != nullptr) {
    for (guint index = 0; index < devices->len; ++index) {
      auto* device = NM_DEVICE(g_ptr_array_index(devices, index));
      const NMDeviceType type = nm_device_get_device_type(device);
      if (type != NM_DEVICE_TYPE_ETHERNET && type != NM_DEVICE_TYPE_WIFI) {
        continue;
      }
      NetLink link;
      link.kind = type == NM_DEVICE_TYPE_WIFI ? "wifi" : "wired";
      const char* iface = nm_device_get_iface(device);
      link.iface = iface != nullptr ? iface : "";
      link.state = static_cast<int>(nm_device_get_state(device));
      if (NM_IS_DEVICE_WIFI(device)) {
        NMAccessPoint* point = nm_device_wifi_get_active_access_point(NM_DEVICE_WIFI(device));
        if (point != nullptr) {
          link.detail = ssid_text(nm_access_point_get_ssid(point));
        }
      }
      if (link.detail.empty()) {
        NMActiveConnection* active = nm_device_get_active_connection(device);
        if (active != nullptr && nm_active_connection_get_id(active) != nullptr) {
          link.detail = nm_active_connection_get_id(active);
        }
      }
      snapshot.links.push_back(std::move(link));
    }
  }
  const GPtrArray* connections = nm_client_get_connections(client);
  if (connections != nullptr) {
    for (guint index = 0; index < connections->len; ++index) {
      auto* remote = NM_REMOTE_CONNECTION(g_ptr_array_index(connections, index));
      NMSettingConnection* setting = nm_connection_get_setting_connection(NM_CONNECTION(remote));
      if (setting == nullptr) {
        continue;
      }
      NetConnection info;
      const char* uuid = nm_setting_connection_get_uuid(setting);
      const char* name = nm_setting_connection_get_id(setting);
      const char* type = nm_setting_connection_get_connection_type(setting);
      info.uuid = uuid != nullptr ? uuid : "";
      info.name = name != nullptr ? name : info.uuid;
      info.type = type != nullptr ? type : "";
      info.autoconnect = nm_setting_connection_get_autoconnect(setting) != FALSE;
      info.wifi = info.type == NM_SETTING_WIRELESS_SETTING_NAME;
      info.vpn = info.type == NM_SETTING_VPN_SETTING_NAME || info.type == NM_SETTING_WIREGUARD_SETTING_NAME;
      read_ip4(NM_CONNECTION(remote), info);
      if (!info.uuid.empty()) {
        snapshot.connections.push_back(std::move(info));
      }
    }
  }
  g_object_unref(client);
  return snapshot;
}

std::string NmClient::save(const NetConnection& edited, const std::string& password,
                           bool set_password) {
  if (edited.uuid.rfind("sample-", 0) == 0) {
    return {};
  }
  std::string error;
  NMClient* client = open_client(&error);
  if (client == nullptr) {
    return error;
  }
  NMRemoteConnection* remote = connection_by_uuid(client, edited.uuid);
  if (remote == nullptr) {
    g_object_unref(client);
    return "That connection is no longer saved.";
  }
  NMSettingConnection* setting = nm_connection_get_setting_connection(NM_CONNECTION(remote));
  if (setting == nullptr) {
    g_object_unref(client);
    return "That connection cannot be edited.";
  }
  g_object_set(setting, NM_SETTING_CONNECTION_ID, edited.name.c_str(), NM_SETTING_CONNECTION_AUTOCONNECT,
               edited.autoconnect ? TRUE : FALSE, nullptr);
  NMSettingIPConfig* ip4 = nm_connection_get_setting_ip4_config(NM_CONNECTION(remote));
  if (ip4 == nullptr) {
    ip4 = NM_SETTING_IP_CONFIG(nm_setting_ip4_config_new());
    nm_connection_add_setting(NM_CONNECTION(remote), NM_SETTING(ip4));
  }
  const bool manual = edited.ipv4_method == "manual";
  const char* method = manual ? NM_SETTING_IP4_CONFIG_METHOD_MANUAL
                              : (edited.ipv4_method == "auto" ? NM_SETTING_IP4_CONFIG_METHOD_AUTO
                                                             : edited.ipv4_method.c_str());
  g_object_set(ip4, NM_SETTING_IP_CONFIG_METHOD, method, nullptr);
  nm_setting_ip_config_clear_addresses(ip4);
  nm_setting_ip_config_clear_dns(ip4);
  if (manual) {
    g_object_set(ip4, NM_SETTING_IP_CONFIG_GATEWAY,
                 edited.gateway.empty() ? nullptr : edited.gateway.c_str(), nullptr);
    for (const auto& address : edited.addresses) {
      GError* gerror = nullptr;
      NMIPAddress* row =
          nm_ip_address_new(AF_INET, address.address.c_str(), static_cast<guint>(address.prefix), &gerror);
      if (row == nullptr) {
        error = take_error(gerror, "That address was not accepted.");
        g_object_unref(client);
        return error;
      }
      nm_setting_ip_config_add_address(ip4, row);
      nm_ip_address_unref(row);
    }
    for (const auto& dns : edited.dns) {
      nm_setting_ip_config_add_dns(ip4, dns.c_str());
    }
  } else {
    g_object_set(ip4, NM_SETTING_IP_CONFIG_GATEWAY, nullptr, nullptr);
  }
  if (set_password && edited.wifi) {
    NMSettingWirelessSecurity* security =
        nm_connection_get_setting_wireless_security(NM_CONNECTION(remote));
    if (security == nullptr) {
      security = NM_SETTING_WIRELESS_SECURITY(nm_setting_wireless_security_new());
      nm_connection_add_setting(NM_CONNECTION(remote), NM_SETTING(security));
      g_object_set(security, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk", nullptr);
    }
    g_object_set(security, NM_SETTING_WIRELESS_SECURITY_PSK, password.c_str(), nullptr);
  }
  GError* gerror = nullptr;
  if (!nm_remote_connection_commit_changes(remote, TRUE, nullptr, &gerror)) {
    error = take_error(gerror, "NetworkManager did not save that connection.");
  }
  g_object_unref(client);
  return error;
}

std::string NmClient::forget(const std::string& uuid) {
  if (uuid.rfind("sample-", 0) == 0) {
    return {};
  }
  std::string error;
  NMClient* client = open_client(&error);
  if (client == nullptr) {
    return error;
  }
  NMRemoteConnection* remote = connection_by_uuid(client, uuid);
  if (remote == nullptr) {
    g_object_unref(client);
    return {};
  }
  GError* gerror = nullptr;
  if (!nm_remote_connection_delete(remote, nullptr, &gerror)) {
    error = take_error(gerror, "NetworkManager did not forget that connection.");
  }
  g_object_unref(client);
  return error;
}

std::string NmClient::wifi_password(const std::string& uuid, std::string& password) const {
  password.clear();
  if (uuid.rfind("sample-", 0) == 0) {
    password = "sample-secret";
    return {};
  }
  std::string error;
  NMClient* client = open_client(&error);
  if (client == nullptr) {
    return error;
  }
  NMRemoteConnection* remote = connection_by_uuid(client, uuid);
  if (remote == nullptr) {
    g_object_unref(client);
    return "That connection is no longer saved.";
  }
  GError* gerror = nullptr;
  GVariant* secrets = nm_remote_connection_get_secrets(
      remote, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, nullptr, &gerror);
  if (secrets != nullptr) {
    nm_connection_update_secrets(NM_CONNECTION(remote), NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, secrets,
                                 nullptr);
    g_variant_unref(secrets);
  } else if (gerror != nullptr) {
    error = take_error(gerror, "The Wi-Fi password is not available.");
    g_object_unref(client);
    return error;
  }
  NMSettingWirelessSecurity* security =
      nm_connection_get_setting_wireless_security(NM_CONNECTION(remote));
  if (security != nullptr && nm_setting_wireless_security_get_psk(security) != nullptr) {
    password = nm_setting_wireless_security_get_psk(security);
  }
  g_object_unref(client);
  return {};
}

std::string NmClient::import_vpn(const VpnFile& file, const std::string& name) {
  if (file.kind == VpnKind::Unknown) {
    return file.error.empty() ? "That file is not an OpenVPN or WireGuard profile." : file.error;
  }
  std::string error;
  NMClient* client = open_client(&error);
  if (client == nullptr) {
    return error;
  }
  const NetPlugins plugins = detect_plugins();
  NMConnection* connection = nm_simple_connection_new();
  NMSettingConnection* setting = NM_SETTING_CONNECTION(nm_setting_connection_new());
  char* uuid = nm_utils_uuid_generate();
  const std::string id = name.empty() ? "VPN" : name;
  nm_connection_add_setting(connection, NM_SETTING(setting));
  if (file.kind == VpnKind::OpenVpn) {
    if (!plugins.openvpn) {
      g_free(uuid);
      g_object_unref(connection);
      g_object_unref(client);
      return "The NetworkManager OpenVPN plugin is not installed.";
    }
    g_object_set(setting, NM_SETTING_CONNECTION_ID, id.c_str(), NM_SETTING_CONNECTION_UUID, uuid,
                 NM_SETTING_CONNECTION_TYPE, NM_SETTING_VPN_SETTING_NAME, NM_SETTING_CONNECTION_AUTOCONNECT,
                 FALSE, nullptr);
    NMSettingVpn* vpn = NM_SETTING_VPN(nm_setting_vpn_new());
    g_object_set(vpn, NM_SETTING_VPN_SERVICE_TYPE, "org.freedesktop.NetworkManager.openvpn", nullptr);
    const std::string ca = file.openvpn.ca_pem.empty()
                               ? file.openvpn.ca
                               : write_pem(id, "ca", file.openvpn.ca_pem);
    const std::string cert = file.openvpn.cert_pem.empty()
                                 ? file.openvpn.cert
                                 : write_pem(id, "cert", file.openvpn.cert_pem);
    const std::string key = file.openvpn.key_pem.empty()
                                ? file.openvpn.key
                                : write_pem(id, "key", file.openvpn.key_pem);
    const char* vpn_type = "tls";
    if (file.openvpn.user_pass && !cert.empty()) {
      vpn_type = "password-tls";
    } else if (file.openvpn.user_pass) {
      vpn_type = "password";
    }
    nm_setting_vpn_add_data_item(vpn, "connection-type", vpn_type);
    nm_setting_vpn_add_data_item(vpn, "remote", file.openvpn.remote.c_str());
    nm_setting_vpn_add_data_item(vpn, "port", std::to_string(file.openvpn.port).c_str());
    if (!file.openvpn.dev.empty()) {
      nm_setting_vpn_add_data_item(vpn, "dev", file.openvpn.dev.c_str());
    }
    if (file.openvpn.proto.rfind("tcp", 0) == 0) {
      nm_setting_vpn_add_data_item(vpn, "proto-tcp", "yes");
    }
    if (!ca.empty()) {
      nm_setting_vpn_add_data_item(vpn, "ca", ca.c_str());
    }
    if (!cert.empty()) {
      nm_setting_vpn_add_data_item(vpn, "cert", cert.c_str());
    }
    if (!key.empty()) {
      nm_setting_vpn_add_data_item(vpn, "key", key.c_str());
    }
    nm_connection_add_setting(connection, NM_SETTING(vpn));
    NMSetting* ip4 = nm_setting_ip4_config_new();
    g_object_set(ip4, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_AUTO, nullptr);
    if (!file.openvpn.dns.empty()) {
      for (const auto& dns : file.openvpn.dns) {
        if (!ipv6_text(dns)) {
          nm_setting_ip_config_add_dns(NM_SETTING_IP_CONFIG(ip4), dns.c_str());
        }
      }
    }
    nm_connection_add_setting(connection, ip4);
  } else {
    if (!plugins.wireguard) {
      g_free(uuid);
      g_object_unref(connection);
      g_object_unref(client);
      return "NetworkManager's WireGuard support is not installed.";
    }
    g_object_set(setting, NM_SETTING_CONNECTION_ID, id.c_str(), NM_SETTING_CONNECTION_UUID, uuid,
                 NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIREGUARD_SETTING_NAME,
                 NM_SETTING_CONNECTION_AUTOCONNECT, FALSE, nullptr);
    NMSettingWireGuard* wireguard = NM_SETTING_WIREGUARD(nm_setting_wireguard_new());
    g_object_set(wireguard, NM_SETTING_WIREGUARD_PRIVATE_KEY, file.wireguard.private_key.c_str(), nullptr);
    if (file.wireguard.listen_port > 0) {
      g_object_set(wireguard, NM_SETTING_WIREGUARD_LISTEN_PORT,
                   static_cast<guint>(file.wireguard.listen_port), nullptr);
    }
    NMSetting* ip4 = nm_setting_ip4_config_new();
    NMSetting* ip6 = nm_setting_ip6_config_new();
    bool have4 = false;
    bool have6 = false;
    g_object_set(ip4, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_MANUAL, nullptr);
    g_object_set(ip6, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP6_CONFIG_METHOD_MANUAL, nullptr);
    for (const auto& address : file.wireguard.addresses) {
      const auto slash = address.find('/');
      const std::string host = slash == std::string::npos ? address : address.substr(0, slash);
      int prefix = ipv6_text(host) ? 128 : 32;
      if (slash != std::string::npos) {
        try {
          prefix = std::stoi(address.substr(slash + 1));
        } catch (const std::exception&) {
        }
      }
      GError* gerror = nullptr;
      NMIPAddress* row = nm_ip_address_new(ipv6_text(host) ? AF_INET6 : AF_INET, host.c_str(),
                                           static_cast<guint>(prefix), &gerror);
      if (row == nullptr) {
        if (gerror != nullptr) {
          g_error_free(gerror);
        }
        continue;
      }
      if (ipv6_text(host)) {
        nm_setting_ip_config_add_address(NM_SETTING_IP_CONFIG(ip6), row);
        have6 = true;
      } else {
        nm_setting_ip_config_add_address(NM_SETTING_IP_CONFIG(ip4), row);
        have4 = true;
      }
      nm_ip_address_unref(row);
    }
    for (const auto& dns : file.wireguard.dns) {
      if (ipv6_text(dns)) {
        nm_setting_ip_config_add_dns(NM_SETTING_IP_CONFIG(ip6), dns.c_str());
      } else {
        nm_setting_ip_config_add_dns(NM_SETTING_IP_CONFIG(ip4), dns.c_str());
      }
    }
    if (!have4) {
      g_object_set(ip4, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_DISABLED, nullptr);
    }
    if (!have6) {
      g_object_set(ip6, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP6_CONFIG_METHOD_DISABLED, nullptr);
    }
    for (const auto& peer : file.wireguard.peers) {
      NMWireGuardPeer* row = nm_wireguard_peer_new();
      nm_wireguard_peer_set_public_key(row, peer.public_key.c_str(), TRUE);
      if (!peer.preshared_key.empty()) {
        nm_wireguard_peer_set_preshared_key(row, peer.preshared_key.c_str(), TRUE);
      }
      if (!peer.endpoint.empty()) {
        nm_wireguard_peer_set_endpoint(row, peer.endpoint.c_str(), TRUE);
      }
      for (const auto& allowed : peer.allowed_ips) {
        nm_wireguard_peer_append_allowed_ip(row, allowed.c_str(), TRUE);
      }
      if (peer.keepalive > 0) {
        nm_wireguard_peer_set_persistent_keepalive(row, static_cast<guint16>(peer.keepalive));
      }
      nm_setting_wireguard_append_peer(wireguard, row);
      nm_wireguard_peer_unref(row);
    }
    nm_connection_add_setting(connection, NM_SETTING(wireguard));
    nm_connection_add_setting(connection, ip4);
    nm_connection_add_setting(connection, ip6);
  }
  g_free(uuid);
  error = add_connection(client, connection);
  g_object_unref(connection);
  g_object_unref(client);
  return error;
}

}  // namespace lcos
