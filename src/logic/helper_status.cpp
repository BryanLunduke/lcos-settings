// SPDX-License-Identifier: GPL-3.0-or-later

#include "helper_status.hpp"

#include <sys/wait.h>

#include <string>
#include <vector>

namespace lcos {
namespace {

std::string trim_line(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
    text.pop_back();
  }
  std::size_t start = 0;
  while (start < text.size() && text[start] == ' ') {
    ++start;
  }
  if (start > 0) {
    text.erase(0, start);
  }
  return text;
}

// The helper prints a generic header and then one line per item that failed.
// The dialog has to name those items, not only the header.
std::string failure_text(const std::string& stderr_text) {
  std::vector<std::string> lines;
  std::string line;
  for (std::size_t i = 0; i <= stderr_text.size(); ++i) {
    if (i == stderr_text.size() || stderr_text[i] == '\n') {
      line = trim_line(line);
      if (!line.empty()) {
        lines.push_back(line);
      }
      line.clear();
    } else {
      line.push_back(stderr_text[i]);
    }
  }
  if (lines.empty()) {
    return {};
  }
  std::size_t begin = 0;
  if (lines.size() >= 2 && lines[0] == "could not restore every change") {
    begin = 1;
  }
  std::string out;
  for (std::size_t i = begin; i < lines.size(); ++i) {
    if (!out.empty()) {
      out.push_back('\n');
    }
    out += lines[i];
  }
  return out;
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
  const std::string err = failure_text(stderr_text);
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
