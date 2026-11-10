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

// xfconf /Xft/DPI is plain DPI. xfsettingsd multiplies by 1024 only for the
// XSETTINGS wire value. Values above 500 are read as the old scaled form.
int xft_dpi_to_spin(int stored);
int spin_to_xft_dpi(int spin);

}  // namespace lcos

#endif
