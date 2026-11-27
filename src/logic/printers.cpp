// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/printers.hpp"

#include <cctype>

namespace lcos {
namespace {

std::string lower_copy(const std::string& text) {
  std::string out = text;
  for (char& character : out) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return out;
}

}  // namespace

bool printer_reasons_paused(const std::string& reasons) {
  return lower_copy(reasons).find("paused") != std::string::npos;
}

std::string printer_status_line(const PrinterRecord& printer) {
  std::string text;
  if (printer.paused || printer_reasons_paused(printer.reasons)) {
    text = "Paused";
  } else if (printer.ipp_state == 4) {
    text = "Printing";
  } else if (printer.ipp_state == 5) {
    text = "Stopped";
  } else if (printer.ipp_state == 3) {
    text = "Idle";
  } else {
    text = "Unknown";
  }
  if (printer.is_default) {
    text += " · Default";
  }
  return text;
}

std::string job_state_line(int ipp_job_state) {
  switch (ipp_job_state) {
    case 3:
      return "Pending";
    case 4:
      return "Held";
    case 5:
      return "Printing";
    case 6:
      return "Stopped";
    case 7:
      return "Canceled";
    case 8:
      return "Aborted";
    case 9:
      return "Completed";
    default:
      return "Unknown";
  }
}

}  // namespace lcos
