// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/network_model.hpp"

#include <cctype>
#include <sstream>

namespace lcos {
namespace {

std::string trim(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return text.substr(begin, end - begin);
}

std::vector<std::string> words(const std::string& text) {
  std::vector<std::string> parts;
  std::string current;
  for (char character : text) {
    if (std::isspace(static_cast<unsigned char>(character))) {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(character);
    }
  }
  if (!current.empty()) {
    parts.push_back(current);
  }
  return parts;
}

std::vector<std::string> split_commas(const std::string& text) {
  std::vector<std::string> parts;
  std::string current;
  for (char character : text) {
    if (character == ',' || std::isspace(static_cast<unsigned char>(character))) {
      const std::string item = trim(current);
      if (!item.empty()) {
        parts.push_back(item);
      }
      current.clear();
    } else {
      current.push_back(character);
    }
  }
  const std::string item = trim(current);
  if (!item.empty()) {
    parts.push_back(item);
  }
  return parts;
}

bool octet(const std::string& text, int& value) {
  if (text.empty() || text.size() > 3) {
    return false;
  }
  value = 0;
  for (char character : text) {
    if (character < '0' || character > '9') {
      return false;
    }
    value = value * 10 + (character - '0');
  }
  return value <= 255;
}

std::string filename_stem(const std::string& filename) {
  std::string name = filename;
  const auto slash = name.find_last_of("/\\");
  if (slash != std::string::npos) {
    name = name.substr(slash + 1);
  }
  const auto dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) {
    name = name.substr(0, dot);
  }
  std::string clean;
  for (char character : name) {
    const unsigned char value = static_cast<unsigned char>(character);
    if (std::isalnum(value) || character == '-' || character == '_' || character == ' ') {
      clean.push_back(character);
    }
  }
  clean = trim(clean);
  return clean.empty() ? "VPN" : clean;
}

bool looks_like_wireguard(const std::string& text) {
  return text.find("[Interface]") != std::string::npos && text.find("PrivateKey") != std::string::npos;
}

bool looks_like_openvpn(const std::string& text, const std::string& filename) {
  if (filename.size() >= 5) {
    const std::string tail = filename.substr(filename.size() - 5);
    if (tail == ".ovpn" || tail == ".OVPN") {
      return true;
    }
  }
  return text.find("\nremote ") != std::string::npos || text.rfind("remote ", 0) == 0 ||
         text.find("<ca>") != std::string::npos || text.find("\nclient") != std::string::npos ||
         text.rfind("client", 0) == 0;
}

VpnFile parse_openvpn(const std::string& text) {
  VpnFile file;
  file.kind = VpnKind::OpenVpn;
  std::string block;
  std::string block_body;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::string trimmed = trim(line);
    if (block.empty() && (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')) {
      continue;
    }
    if (block.empty() && trimmed.size() > 2 && trimmed.front() == '<' && trimmed.back() == '>' &&
        trimmed[1] != '/') {
      block = trimmed.substr(1, trimmed.size() - 2);
      block_body.clear();
      continue;
    }
    if (!block.empty()) {
      if (trimmed == "</" + block + ">") {
        if (!block_body.empty() && block_body.back() == '\n') {
          block_body.pop_back();
        }
        if (block == "ca") {
          file.openvpn.ca_pem = block_body;
        } else if (block == "cert") {
          file.openvpn.cert_pem = block_body;
        } else if (block == "key") {
          file.openvpn.key_pem = block_body;
        }
        block.clear();
      } else {
        block_body += line;
        block_body.push_back('\n');
      }
      continue;
    }
    const auto tokens = words(trimmed);
    if (tokens.empty()) {
      continue;
    }
    const std::string& key = tokens[0];
    if (key == "remote" && tokens.size() >= 2) {
      file.openvpn.remote = tokens[1];
      if (tokens.size() >= 3) {
        try {
          file.openvpn.port = std::stoi(tokens[2]);
        } catch (const std::exception&) {
          file.openvpn.port = 1194;
        }
      }
      if (tokens.size() >= 4) {
        file.openvpn.proto = tokens[3];
      }
    } else if (key == "proto" && tokens.size() >= 2) {
      file.openvpn.proto = tokens[1];
    } else if (key == "port" && tokens.size() >= 2) {
      try {
        file.openvpn.port = std::stoi(tokens[1]);
      } catch (const std::exception&) {
      }
    } else if (key == "dev" && tokens.size() >= 2) {
      file.openvpn.dev = tokens[1];
    } else if (key == "ca" && tokens.size() >= 2) {
      file.openvpn.ca = tokens[1];
    } else if (key == "cert" && tokens.size() >= 2) {
      file.openvpn.cert = tokens[1];
    } else if (key == "key" && tokens.size() >= 2) {
      file.openvpn.key = tokens[1];
    } else if (key == "auth-user-pass") {
      file.openvpn.user_pass = true;
    } else if (key == "dhcp-option" && tokens.size() >= 3 &&
               (tokens[1] == "DNS" || tokens[1] == "dns")) {
      file.openvpn.dns.push_back(tokens[2]);
    }
  }
  if (file.openvpn.remote.empty()) {
    file.kind = VpnKind::Unknown;
    file.error = "That OpenVPN file has no remote server.";
  } else if (file.openvpn.port < 1 || file.openvpn.port > 65535) {
    file.kind = VpnKind::Unknown;
    file.error = "That OpenVPN file has a port Command Center cannot use.";
  }
  return file;
}

VpnFile parse_wireguard(const std::string& text) {
  VpnFile file;
  file.kind = VpnKind::WireGuard;
  std::string section;
  WgPeer peer;
  bool have_peer = false;
  auto finish_peer = [&]() {
    if (!have_peer) {
      return;
    }
    if (!peer.public_key.empty()) {
      file.wireguard.peers.push_back(peer);
    }
    peer = {};
    have_peer = false;
  };
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
      continue;
    }
    if (trimmed.front() == '[' && trimmed.back() == ']') {
      finish_peer();
      section = trimmed.substr(1, trimmed.size() - 2);
      if (section == "Peer") {
        have_peer = true;
        peer = {};
      }
      continue;
    }
    const auto equal = trimmed.find('=');
    if (equal == std::string::npos) {
      continue;
    }
    const std::string key = trim(trimmed.substr(0, equal));
    const std::string value = trim(trimmed.substr(equal + 1));
    if (section == "Interface") {
      if (key == "PrivateKey") {
        file.wireguard.private_key = value;
      } else if (key == "Address") {
        for (const auto& address : split_commas(value)) {
          file.wireguard.addresses.push_back(address);
        }
      } else if (key == "DNS") {
        for (const auto& dns : split_commas(value)) {
          file.wireguard.dns.push_back(dns);
        }
      } else if (key == "ListenPort") {
        try {
          file.wireguard.listen_port = std::stoi(value);
        } catch (const std::exception&) {
          file.wireguard.listen_port = 0;
        }
      }
    } else if (section == "Peer") {
      have_peer = true;
      if (key == "PublicKey") {
        peer.public_key = value;
      } else if (key == "PresharedKey") {
        peer.preshared_key = value;
      } else if (key == "Endpoint") {
        peer.endpoint = value;
      } else if (key == "AllowedIPs") {
        peer.allowed_ips = split_commas(value);
      } else if (key == "PersistentKeepalive") {
        try {
          peer.keepalive = std::stoi(value);
        } catch (const std::exception&) {
          peer.keepalive = 0;
        }
      }
    }
  }
  finish_peer();
  if (file.wireguard.private_key.empty()) {
    file.kind = VpnKind::Unknown;
    file.error = "That WireGuard file has no private key.";
  } else if (file.wireguard.peers.empty()) {
    file.kind = VpnKind::Unknown;
    file.error = "That WireGuard file has no peer.";
  } else if (file.wireguard.addresses.empty()) {
    file.kind = VpnKind::Unknown;
    file.error = "That WireGuard file has no address.";
  }
  return file;
}

}  // namespace

std::string link_status_text(const NetLink& link) {
  std::string state = "Not connected";
  if (link.state == 100) {
    state = "Connected";
  } else if (link.state == 30) {
    state = "Disconnected";
  } else if (link.state == 20) {
    state = "Unavailable";
  } else if (link.state == 10) {
    state = "Unmanaged";
  } else if (link.state == 120) {
    state = "Failed";
  } else if (link.state == 60) {
    state = "Needs a password";
  } else if (link.state >= 40 && link.state < 100) {
    state = "Connecting";
  }
  std::string text = link.kind == "wifi" ? "Wi-Fi" : "Wired";
  if (!link.iface.empty()) {
    text += " (" + link.iface + ")";
  }
  text += ": " + state;
  if (link.state == 100 && !link.detail.empty()) {
    text += " — " + link.detail;
  }
  return text;
}

std::string connection_type_label(const std::string& type) {
  if (type == "802-11-wireless") {
    return "Wi-Fi";
  }
  if (type == "802-3-ethernet") {
    return "Wired";
  }
  if (type == "vpn") {
    return "VPN";
  }
  if (type == "wireguard") {
    return "WireGuard";
  }
  if (type == "tun" || type == "gsm" || type == "bluetooth") {
    return type;
  }
  return type.empty() ? "Connection" : type;
}

std::string format_dns_list(const std::vector<std::string>& dns) {
  std::string text;
  for (const auto& item : dns) {
    if (item.empty()) {
      continue;
    }
    if (!text.empty()) {
      text += ", ";
    }
    text += item;
  }
  return text;
}

std::vector<std::string> split_dns_list(const std::string& text) { return split_commas(text); }

bool ipv4_host_ok(const std::string& host) {
  int octets[4] = {};
  std::size_t start = 0;
  for (int index = 0; index < 4; ++index) {
    const auto dot = host.find('.', start);
    const std::string part =
        host.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    if (!octet(part, octets[index])) {
      return false;
    }
    if (index < 3) {
      if (dot == std::string::npos) {
        return false;
      }
      start = dot + 1;
    } else if (dot != std::string::npos) {
      return false;
    }
  }
  return true;
}

bool ipv4_cidr_ok(const std::string& text, std::string* host, int* prefix) {
  const auto slash = text.find('/');
  const std::string address = slash == std::string::npos ? text : text.substr(0, slash);
  if (!ipv4_host_ok(address)) {
    return false;
  }
  int bits = 24;
  if (slash != std::string::npos) {
    const std::string tail = text.substr(slash + 1);
    if (tail.empty()) {
      return false;
    }
    bits = 0;
    for (char character : tail) {
      if (character < '0' || character > '9') {
        return false;
      }
      bits = bits * 10 + (character - '0');
    }
    if (bits > 32) {
      return false;
    }
  }
  if (host != nullptr) {
    *host = address;
  }
  if (prefix != nullptr) {
    *prefix = bits;
  }
  return true;
}

std::string ipv4_edit_error(const Ipv4Edit& edit) {
  if (edit.method != "manual") {
    return {};
  }
  if (edit.address.empty()) {
    return "Enter an IPv4 address such as 192.168.1.20.";
  }
  std::string host;
  int prefix = edit.prefix;
  if (edit.address.find('/') != std::string::npos) {
    if (!ipv4_cidr_ok(edit.address, &host, &prefix)) {
      return "Enter an IPv4 address such as 192.168.1.20/24.";
    }
  } else if (!ipv4_host_ok(edit.address)) {
    return "Enter an IPv4 address such as 192.168.1.20.";
  }
  if (prefix < 0 || prefix > 32) {
    return "The network prefix must be from 0 to 32.";
  }
  if (!edit.gateway.empty() && !ipv4_host_ok(edit.gateway)) {
    return "The gateway must be an IPv4 address.";
  }
  for (const auto& dns : split_dns_list(edit.dns)) {
    if (!ipv4_host_ok(dns)) {
      return "Each DNS server must be an IPv4 address.";
    }
  }
  return {};
}

VpnFile parse_vpn_file(const std::string& text, const std::string& filename) {
  if (looks_like_wireguard(text)) {
    return parse_wireguard(text);
  }
  if (looks_like_openvpn(text, filename)) {
    return parse_openvpn(text);
  }
  VpnFile file;
  file.error = "Command Center can import OpenVPN and WireGuard files.";
  return file;
}

std::string vpn_connection_name(const std::string& filename) { return filename_stem(filename); }

}  // namespace lcos
