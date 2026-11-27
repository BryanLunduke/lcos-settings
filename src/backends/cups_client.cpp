// SPDX-License-Identifier: GPL-3.0-or-later

#include "backends/cups_client.hpp"

#include <cups/cups.h>
#include <cups/ipp.h>

#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unistd.h>
#include <vector>

namespace lcos {
namespace {

const char* refuse_password(const char*, http_t*, const char*, const char*, void*) { return nullptr; }

http_t* connect_cups(std::string* error) {
  static bool password_set = false;
  if (!password_set) {
    cupsSetPasswordCB2(refuse_password, nullptr);
    password_set = true;
  }
  http_t* http = httpConnect2(cupsServer(), ippPort(), nullptr, AF_UNSPEC, cupsEncryption(), 1, 2000,
                              nullptr);
  if (http == nullptr) {
    if (error != nullptr) {
      *error = "CUPS is not running.";
    }
  }
  return http;
}

std::string printer_uri(const std::string& name) {
  char uri[HTTP_MAX_URI];
  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", nullptr, cupsServer(), ippPort(),
                   "/printers/%s", name.c_str());
  return uri;
}

std::string ipp_admin(http_t* http, ipp_op_t op, const std::string& name, int job_id) {
  ipp_t* request = ippNewRequest(op);
  const std::string uri = printer_uri(name);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", nullptr, uri.c_str());
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", nullptr, cupsUser());
  if (job_id > 0) {
    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", job_id);
  }
  const char* resource = op == IPP_OP_CANCEL_JOB ? "/jobs/" : "/admin/";
  ipp_t* response = cupsDoRequest(http, request, resource);
  const ipp_status_t status = cupsLastError();
  std::string message;
  if (response == nullptr || status >= IPP_STATUS_ERROR_BAD_REQUEST) {
    const char* text = cupsLastErrorString();
    message = text != nullptr && text[0] != '\0' ? text : "CUPS refused that request.";
  }
  if (response != nullptr) {
    ippDelete(response);
  }
  return message;
}

PrinterSnapshot sample_snapshot(const std::string& jobs_for) {
  PrinterSnapshot snapshot;
  snapshot.available = true;
  PrinterRecord office;
  office.name = "Office";
  office.detail = "Ready";
  office.ipp_state = 3;
  office.is_default = true;
  PrinterRecord label;
  label.name = "Label";
  label.detail = "Paused";
  label.ipp_state = 5;
  label.reasons = "paused";
  label.paused = true;
  snapshot.printers.push_back(office);
  snapshot.printers.push_back(label);
  if (jobs_for.empty() || jobs_for == "Office") {
    PrintJob job;
    job.id = 12;
    job.printer = "Office";
    job.title = "Notes";
    job.user = "lcos";
    job.state = 5;
    snapshot.jobs.push_back(job);
  }
  return snapshot;
}

}  // namespace

PrinterSnapshot CupsClient::snapshot(const std::string& jobs_for, bool sample) const {
  if (sample) {
    return sample_snapshot(jobs_for);
  }
  PrinterSnapshot snapshot;
  std::string error;
  http_t* http = connect_cups(&error);
  if (http == nullptr) {
    snapshot.error = error.empty() ? "CUPS is not running." : error;
    return snapshot;
  }
  cups_dest_t* dests = nullptr;
  const int count = cupsGetDests2(http, &dests);
  snapshot.available = true;
  for (int index = 0; index < count; ++index) {
    if (dests[index].instance != nullptr) {
      continue;
    }
    PrinterRecord printer;
    printer.name = dests[index].name != nullptr ? dests[index].name : "";
    printer.is_default = dests[index].is_default != 0;
    const char* info =
        cupsGetOption("printer-info", dests[index].num_options, dests[index].options);
    const char* state =
        cupsGetOption("printer-state", dests[index].num_options, dests[index].options);
    const char* reasons =
        cupsGetOption("printer-state-reasons", dests[index].num_options, dests[index].options);
    printer.detail = info != nullptr ? info : "";
    printer.reasons = reasons != nullptr ? reasons : "";
    if (state != nullptr) {
      printer.ipp_state = std::atoi(state);
    }
    printer.paused = printer_reasons_paused(printer.reasons);
    if (!printer.name.empty()) {
      snapshot.printers.push_back(std::move(printer));
    }
  }
  cupsFreeDests(count, dests);
  if (!jobs_for.empty()) {
    cups_job_t* jobs = nullptr;
    const int job_count = cupsGetJobs2(http, &jobs, jobs_for.c_str(), 0, CUPS_WHICHJOBS_ACTIVE);
    for (int index = 0; index < job_count; ++index) {
      PrintJob job;
      job.id = jobs[index].id;
      job.printer = jobs[index].dest != nullptr ? jobs[index].dest : jobs_for;
      job.title = jobs[index].title != nullptr ? jobs[index].title : "Print job";
      job.user = jobs[index].user != nullptr ? jobs[index].user : "";
      job.state = static_cast<int>(jobs[index].state);
      snapshot.jobs.push_back(std::move(job));
    }
    cupsFreeJobs(job_count, jobs);
  }
  httpClose(http);
  return snapshot;
}

std::string CupsClient::set_default(const std::string& name) {
  std::string error;
  http_t* http = connect_cups(&error);
  if (http == nullptr) {
    return error;
  }
  error = ipp_admin(http, IPP_OP_CUPS_SET_DEFAULT, name, 0);
  httpClose(http);
  return error;
}

std::string CupsClient::set_paused(const std::string& name, bool paused) {
  std::string error;
  http_t* http = connect_cups(&error);
  if (http == nullptr) {
    return error;
  }
  error = ipp_admin(http, paused ? IPP_OP_PAUSE_PRINTER : IPP_OP_RESUME_PRINTER, name, 0);
  httpClose(http);
  return error;
}

std::string CupsClient::print_test_page(const std::string& name) {
  std::string error;
  http_t* http = connect_cups(&error);
  if (http == nullptr) {
    return error;
  }
  char path[] = "/tmp/lcos-testpage-XXXXXX";
  const int fd = mkstemp(path);
  if (fd < 0) {
    httpClose(http);
    return "Could not write a test page.";
  }
  const char* body =
      "LCOS Command Center\n\nThis is a printer test page.\nIf you can read this, the queue accepted the job.\n";
  if (write(fd, body, std::strlen(body)) < 0) {
    close(fd);
    unlink(path);
    httpClose(http);
    return "Could not write a test page.";
  }
  close(fd);
  const int job = cupsPrintFile2(http, name.c_str(), path, "LCOS test page", 0, nullptr);
  unlink(path);
  if (job == 0) {
    const char* text = cupsLastErrorString();
    error = text != nullptr && text[0] != '\0' ? text : "CUPS did not accept the test page.";
  }
  httpClose(http);
  return error;
}

std::string CupsClient::remove_printer(const std::string& name) {
  std::string error;
  http_t* http = connect_cups(&error);
  if (http == nullptr) {
    return error;
  }
  error = ipp_admin(http, IPP_OP_CUPS_DELETE_PRINTER, name, 0);
  httpClose(http);
  return error;
}

std::string CupsClient::cancel_job(const std::string& printer, int job_id) {
  std::string error;
  http_t* http = connect_cups(&error);
  if (http == nullptr) {
    return error;
  }
  const ipp_status_t status = cupsCancelJob2(http, printer.c_str(), job_id, 0);
  if (status >= IPP_STATUS_ERROR_BAD_REQUEST) {
    const char* text = cupsLastErrorString();
    error = text != nullptr && text[0] != '\0' ? text : "CUPS did not cancel that job.";
  }
  httpClose(http);
  return error;
}

}  // namespace lcos
