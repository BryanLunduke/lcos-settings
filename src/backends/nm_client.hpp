// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_NM_CLIENT_HPP
#define LCOS_NM_CLIENT_HPP

#include "logic/network_model.hpp"

#include <string>

namespace lcos {

// NetworkManager via libnm. Does not edit resolv.conf or install resolvconf.
class NmClient {
 public:
  NetSnapshot snapshot(bool sample) const;
  std::string save(const NetConnection& edited, const std::string& password, bool set_password);
  std::string forget(const std::string& uuid);
  std::string wifi_password(const std::string& uuid, std::string& password) const;
  std::string import_vpn(const VpnFile& file, const std::string& name);
};

}  // namespace lcos

#endif
