// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_PRIV_CLIENT_HPP
#define LCOS_PRIV_CLIENT_HPP

#include <functional>
#include <string>
#include <vector>

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
  bool busy() const { return busy_; }

  // Blocks until pkexec finishes. Reads both pipes so a noisy child cannot
  // stall, and reports the real exit status.
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
  PrivResult revert();

 private:
  struct AsyncOp;

  PrivResult start(const std::vector<std::string>& args, const std::string& input, bool async,
                   std::function<void(PrivResult)> done);
  void finish_async(const PrivResult& result);

  std::string token_;
  bool dirty_ = false;
  bool finished_ = false;
  bool busy_ = false;
  bool cancel_requested_ = false;
  AsyncOp* async_ = nullptr;
};

std::string privileged_helper_path();

}  // namespace lcos

#endif
