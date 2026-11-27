// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_CUPS_CLIENT_HPP
#define LCOS_CUPS_CLIENT_HPP

#include "logic/printers.hpp"

#include <string>

namespace lcos {

// CUPS over IPP. A missing cupsd is reported on the snapshot; calls do not throw.
class CupsClient {
 public:
  PrinterSnapshot snapshot(const std::string& jobs_for, bool sample) const;
  std::string set_default(const std::string& name);
  std::string set_paused(const std::string& name, bool paused);
  std::string print_test_page(const std::string& name);
  std::string remove_printer(const std::string& name);
  std::string cancel_job(const std::string& printer, int job_id);
};

}  // namespace lcos

#endif
