// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_HOST_INFO_HPP
#define LCOS_LOGIC_HOST_INFO_HPP

#include <string>

namespace lcos {

struct OsIdentity {
  std::string name;
  std::string version;
  std::string pretty_name;
  // Small-caps line above the version. Comes from NAME, not a hard-coded brand.
  std::string kicker;
  // Large line, NAME plus VERSION when both exist.
  std::string version_line;
};

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
  std::string kicker;
  std::string version_line;
  std::string hostname;
  std::string processor;
  std::string memory;
  std::string graphics;
  std::string storage;
};

// Reads /etc/os-release, /proc, sysfs, and pci.ids. Missing pieces stay empty
// or say they are unavailable. Nothing here is a placeholder model name.
HostFacts read_host_facts();

}  // namespace lcos

#endif
