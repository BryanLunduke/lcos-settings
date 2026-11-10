// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_HELPER_STATUS_HPP
#define LCOS_LOGIC_HELPER_STATUS_HPP

#include <string>

namespace lcos {

struct HelperExit {
  bool ok = false;
  std::string error;
};

// `wait_result` is the return of waitpid (negative means the child was not
// reaped). `status` is the raw wait status. pkexec uses 126 when the
// authentication dialog is dismissed and 127 when authorization is denied or
// the program cannot be started.
HelperExit interpret_helper_exit(int wait_result, int status, const std::string& stderr_text);

// Xft stores DPI in 1024ths. 96 DPI is 98304. Values of 500 or less are the
// legacy plain number this program used to write.
int xft_dpi_to_spin(int stored);
int spin_to_xft_dpi(int spin);

}  // namespace lcos

#endif
