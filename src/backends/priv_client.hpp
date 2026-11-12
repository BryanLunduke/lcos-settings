// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_PRIV_CLIENT_HPP
#define LCOS_PRIV_CLIENT_HPP

#include "logic/system_model.hpp"

#include <functional>
#include <string>
#include <vector>

#include <sigc++/signal.h>

namespace lcos {

struct PrivResult {
  bool ok = false;
  std::string error;
};

// One undo token for this visit. Cancel restores privileged changes.
// OK drops the undo record. Apply does not, so another privileged change
// can still be made in this visit. Passwords are stdin only.
class PrivSession {
 public:
  PrivSession();
  ~PrivSession();

  PrivSession(const PrivSession&) = delete;
  PrivSession& operator=(const PrivSession&) = delete;

  const std::string& token() const { return token_; }
  bool dirty() const { return dirty_; }
  bool accounts_dirty() const { return touched_accounts_; }
  bool locale_dirty() const { return touched_locale_; }
  bool system_dirty() const { return touched_system_; }
  bool busy() const { return busy_; }

  // Blocks until pkexec finishes. Reads both pipes so a noisy child cannot
  // stall, and reports the real exit status. The helper domain is chosen
  // from the action, so account and locale changes are not covered by the
  // clock action's cached authentication.
  PrivResult run(const std::vector<std::string>& args, const std::string& input = {});

  // Returns as soon as the child is running. `done` runs on the main loop.
  // Used for locale-gen, which has to stay off the GTK thread.
  void run_async(const std::vector<std::string>& args, const std::string& input,
                 std::function<void(PrivResult)> done);
  // Ask the running helper to stop. Writes a cancel line on the helper's
  // stdin and also signals the process group, which only works before
  // pkexec becomes root. The async callback still runs.
  void cancel();
  // Pumps the main loop until an async child has finished.
  void wait_until_idle();

  PrivResult commit();
  // Drops the undo record for changes made so far and leaves the session
  // open, so a later Cancel only restores work done after this Apply.
  PrivResult checkpoint();
  PrivResult revert();

  sigc::signal<void> changed;

 private:
  struct AsyncOp;

  PrivResult start(const std::vector<std::string>& args, const std::string& input, bool async,
                   std::function<void(PrivResult)> done);
  void finish_async(const PrivResult& result);

  PrivResult run_domain(PrivDomain domain, const std::vector<std::string>& args,
                       const std::string& input, bool async, std::function<void(PrivResult)> done);
  PrivResult finish_domains(const char* action, bool end_session);
  void note_success(PrivDomain domain, const std::string& action, bool ok, const std::string& error);

  std::string token_;
  bool dirty_ = false;
  bool finished_ = false;
  bool busy_ = false;
  bool cancel_requested_ = false;
  PrivDomain active_domain_ = PrivDomain::System;
  bool touched_system_ = false;
  bool touched_accounts_ = false;
  bool touched_locale_ = false;
  AsyncOp* async_ = nullptr;
};

std::string privileged_helper_path();

}  // namespace lcos

#endif
