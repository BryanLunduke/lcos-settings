// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_CONFIRM_HPP
#define LCOS_LOGIC_CONFIRM_HPP

namespace lcos {

// The display confirm dialog counts down once per second from this value.
// Reaching zero reverts, the same way xfce4-display-settings does.
inline constexpr int kDisplayConfirmSeconds = 10;

struct ConfirmCountdown {
  int seconds = kDisplayConfirmSeconds;
  bool decided = false;
  bool keep = false;

  void tick() {
    if (decided) {
      return;
    }
    if (--seconds <= 0) {
      seconds = 0;
      decided = true;
      keep = false;
    }
  }

  void accept() {
    decided = true;
    keep = true;
  }

  void reject() {
    decided = true;
    keep = false;
  }
};

}  // namespace lcos

#endif
