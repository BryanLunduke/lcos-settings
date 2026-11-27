// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_NETWORK_MODEL_HPP
#define LCOS_LOGIC_NETWORK_MODEL_HPP

#include <string>
#include <vector>

namespace lcos {

struct NetLink {
  std::string kind;
  std::string iface;
  int state = 0;
  std::string detail;
};

struct NetAddress {
  std::string address;
  int prefix = 24;
};

struct NetConnection {
  std::string uuid;
  std::string name;
  std::string type;
  bool autoconnect = true;
  bool wifi = false;
  bool vpn = false;
  std::string ipv4_method = "auto";
  std::vector<NetAddress> addresses;
  std::string gateway;
  std::vector<std::string> dns;
};

struct NetPlugins {
  bool openvpn = false;
  bool wireguard = false;
};

struct NetSnapshot {
  bool available = false;
  std::string error;
  std::vector<NetLink> links;
  std::vector<NetConnection> connections;
  NetPlugins plugins;
};

struct Ipv4Edit {
  std::string method = "auto";
  std::string address;
  int prefix = 24;
  std::string gateway;
  std::string dns;
};

struct WgPeer {
  std::string public_key;
  std::string preshared_key;
  std::string endpoint;
  std::vector<std::string> allowed_ips;
  int keepalive = 0;
};

struct OpenVpnProfile {
  std::string remote;
  int port = 1194;
  std::string proto = "udp";
  std::string dev = "tun";
  std::string ca;
  std::string cert;
  std::string key;
  std::string ca_pem;
  std::string cert_pem;
  std::string key_pem;
  bool user_pass = false;
  std::vector<std::string> dns;
};

struct WireGuardProfile {
  std::string private_key;
  std::vector<std::string> addresses;
  std::vector<std::string> dns;
  int listen_port = 0;
  std::vector<WgPeer> peers;
};

enum class VpnKind { Unknown, OpenVpn, WireGuard };

struct VpnFile {
  VpnKind kind = VpnKind::Unknown;
  std::string error;
  OpenVpnProfile openvpn;
  WireGuardProfile wireguard;
};

std::string link_status_text(const NetLink& link);
std::string connection_type_label(const std::string& type);
std::string format_dns_list(const std::vector<std::string>& dns);
std::vector<std::string> split_dns_list(const std::string& text);
bool ipv4_host_ok(const std::string& host);
bool ipv4_cidr_ok(const std::string& text, std::string* host, int* prefix);
// Empty when the edit can be saved. Manual checks the address, gateway, and DNS.
std::string ipv4_edit_error(const Ipv4Edit& edit);
VpnFile parse_vpn_file(const std::string& text, const std::string& filename);
std::string vpn_connection_name(const std::string& filename);

}  // namespace lcos

#endif
