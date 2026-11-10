// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_PRIV_CLIENT_HPP
#define LCOS_PRIV_CLIENT_HPP

#include <string>
#include <vector>

namespace lcos {

struct PrivResult {
  bool ok = false;
  std::string error;
};

// One undo token for this visit. Cancel restores privileged changes.
// OK and Apply drop the undo record. Passwords are stdin only.
class PrivSession {
 public:
  PrivSession();

  const std::string& token() const { return token_; }
  bool dirty() const { return dirty_; }
  PrivResult run(const std::vector<std::string>& args, const std::string& input = {});
  PrivResult commit();
  PrivResult revert();

 private:
  std::string token_;
  bool dirty_ = false;
  bool finished_ = false;
};

std::string privileged_helper_path();

}  // namespace lcos

#endif
