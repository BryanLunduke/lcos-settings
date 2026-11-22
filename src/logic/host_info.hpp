// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_HOST_INFO_HPP
#define LCOS_LOGIC_HOST_INFO_HPP

#include <string>

namespace lcos {

struct OsIdentity {
  std::string name;
  std::string id;
  std::string version;
  std::string pretty_name;
  std::string codename;
  // Small-caps line above the version. Comes from NAME, not a hard-coded brand.
  std::string kicker;
  // Large line. PRETTY_NAME when the file has one, otherwise NAME plus VERSION.
  std::string version_line;
  // version_line, plus the codename in parentheses when the line does not
  // already contain it.
  std::string version_detail;
};

// True when NAME, ID, or PRETTY_NAME identifies LCOS.
bool os_identity_is_lcos(const OsIdentity& identity);

struct ProcessorInfo {
  std::string model;
  int threads = 0;
  std::string label;
};

OsIdentity parse_os_release(const std::string& text);
ProcessorInfo parse_cpuinfo(const std::string& text);
// MemTotal in KiB, or 0 when the key is absent.
long long meminfo_total_kib(const std::string& text);
std::string format_gigabytes(long long bytes);
// Device name from a pci.ids document. Empty when the id is not listed.
std::string pci_device_name(const std::string& pci_ids_text, unsigned vendor, unsigned device);

struct HostFacts {
  bool lcos = false;
  std::string kicker;
  std::string version_line;
  std::string version_detail;
  std::string hostname;
  std::string processor;
  std::string memory;
  std::string graphics;
  std::string storage;
  std::string kernel;
  // "XLibre <version>" when the server identifies itself, otherwise empty.
  // The binary is still named Xorg. The label never says Xorg.
  std::string xserver;
  std::string uptime;
  std::string resolution;
};

// "XLibre 25.0.0.8" from an `Xorg -version` dump. XLibre builds that still
// print "X.Org X Server" are labelled XLibre as well. Empty when no version
// line is present.
std::string xlibre_version_label(const std::string& version_text);
std::string format_uptime_seconds(long long seconds);
std::string format_pixels(int width, int height);

// Plain text for a bug report. Empty fields are written as "Unavailable".
std::string system_info_report(const HostFacts& facts);

// Reads /etc/os-release, /proc, sysfs, and pci.ids. Missing pieces stay empty
// or say they are unavailable. Nothing here is a placeholder model name.
HostFacts read_host_facts();

}  // namespace lcos

#endif
