// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_PRINTERS_HPP
#define LCOS_LOGIC_PRINTERS_HPP

#include <string>
#include <vector>

namespace lcos {

struct PrinterRecord {
  std::string name;
  std::string detail;
  int ipp_state = 0;
  std::string reasons;
  bool is_default = false;
  bool paused = false;
};

struct PrintJob {
  int id = 0;
  std::string printer;
  std::string title;
  std::string user;
  int state = 0;
};

struct PrinterSnapshot {
  bool available = false;
  std::string error;
  std::vector<PrinterRecord> printers;
  std::vector<PrintJob> jobs;
};

// IPP printer-state-reasons contains "paused" when the queue is paused.
bool printer_reasons_paused(const std::string& reasons);
std::string printer_status_line(const PrinterRecord& printer);
std::string job_state_line(int ipp_job_state);

}  // namespace lcos

#endif
