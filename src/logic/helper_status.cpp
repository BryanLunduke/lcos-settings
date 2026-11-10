// SPDX-License-Identifier: GPL-3.0-or-later

#include "helper_status.hpp"

#include <sys/wait.h>

#include <string>

namespace lcos {
namespace {

std::string first_line(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  const auto nl = text.find('\n');
  if (nl != std::string::npos) {
    text.resize(nl);
  }
  return text;
}

}  // namespace

HelperExit interpret_helper_exit(int wait_result, int status, const std::string& stderr_text) {
  HelperExit out;
  if (wait_result < 0) {
    out.error = "Could not read the privileged helper result.";
    return out;
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    out.ok = true;
    return out;
  }
  const std::string err = first_line(stderr_text);
  if (!WIFEXITED(status)) {
    out.error = err.empty() ? "The privileged helper did not finish." : err;
    return out;
  }
  const int code = WEXITSTATUS(status);
  if (!err.empty()) {
    out.error = err;
    return out;
  }
  if (code == 126) {
    out.error = "Authentication was dismissed.";
  } else if (code == 127) {
    out.error = "Authentication was denied.";
  } else {
    out.error = "The change was not applied (exit " + std::to_string(code) + ").";
  }
  return out;
}

int xft_dpi_to_spin(int stored) {
  if (stored <= 0) {
    return -1;
  }
  // xfconf /Xft/DPI is plain DPI. A value above 500 is the mistaken
  // 1024-scaled number from an earlier build; show the usable DPI.
  if (stored > 500) {
    const int spin = stored / 1024;
    return spin > 0 ? spin : 1;
  }
  return stored;
}

int spin_to_xft_dpi(int spin) {
  if (spin <= 0) {
    return -1;
  }
  // xfsettingsd multiplies this by 1024 when it publishes the XSETTINGS
  // wire value. The xfconf property itself stays plain DPI.
  return spin;
}

}  // namespace lcos
