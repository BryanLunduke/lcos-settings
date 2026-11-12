// SPDX-License-Identifier: GPL-3.0-or-later

#include "host_info.hpp"

#include "text.hpp"

#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace lcos {
namespace fs = std::filesystem;

namespace {

std::string unquote(std::string value) {
  value = trim_copy(value);
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\''))) {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

std::string read_file(const fs::path& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool hex_byte(const std::string& text, unsigned& value) {
  if (text.empty()) {
    return false;
  }
  try {
    std::size_t used = 0;
    value = static_cast<unsigned>(std::stoul(text, &used, 16));
    return used == text.size();
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace

OsIdentity parse_os_release(const std::string& text) {
  OsIdentity id;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    line = trim_copy(line);
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = trim_copy(line.substr(0, eq));
    const std::string value = unquote(line.substr(eq + 1));
    if (key == "NAME") {
      id.name = value;
    } else if (key == "ID") {
      id.id = value;
    } else if (key == "VERSION") {
      id.version = value;
    } else if (key == "VERSION_ID" && id.version.empty()) {
      id.version = value;
    } else if (key == "PRETTY_NAME") {
      id.pretty_name = value;
    }
  }
  if (!id.pretty_name.empty()) {
    id.version_line = id.pretty_name;
  } else if (!id.name.empty() && !id.version.empty()) {
    id.version_line = id.name + " " + id.version;
  } else if (!id.name.empty()) {
    id.version_line = id.name;
  }
  id.kicker = id.name;
  return id;
}

bool os_identity_is_lcos(const OsIdentity& identity) {
  auto has = [](std::string text) {
    for (char& c : text) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text.find("lcos") != std::string::npos;
  };
  return has(identity.name) || has(identity.id) || has(identity.pretty_name);
}

ProcessorInfo parse_cpuinfo(const std::string& text) {
  ProcessorInfo info;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    const std::string key = trim_copy(line.substr(0, colon));
    const std::string value = trim_copy(line.substr(colon + 1));
    if (key == "processor") {
      ++info.threads;
    } else if (info.model.empty() && (key == "model name" || key == "Hardware" || key == "Model")) {
      info.model = value;
    }
  }
  if (info.model.empty()) {
    info.label = info.threads > 0 ? "Unknown processor" : "";
  } else if (info.threads > 1) {
    info.label = info.model + " × " + std::to_string(info.threads);
  } else {
    info.label = info.model;
  }
  return info;
}

long long meminfo_total_kib(const std::string& text) {
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.compare(0, 9, "MemTotal:") != 0) {
      continue;
    }
    std::istringstream row(line.substr(9));
    long long kib = 0;
    row >> kib;
    return kib > 0 ? kib : 0;
  }
  return 0;
}

std::string format_gigabytes(long long bytes) {
  if (bytes < 0) {
    bytes = 0;
  }
  const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
  if (gb >= 10.0 || std::abs(gb - std::round(gb)) < 0.08) {
    return std::to_string(static_cast<long long>(std::llround(gb))) + " GB";
  }
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(1);
  out << gb << " GB";
  return out.str();
}

std::string pci_device_name(const std::string& pci_ids_text, unsigned vendor, unsigned device) {
  char vendor_key[8];
  char device_key[8];
  std::snprintf(vendor_key, sizeof vendor_key, "%04x", vendor & 0xffff);
  std::snprintf(device_key, sizeof device_key, "%04x", device & 0xffff);
  std::istringstream in(pci_ids_text);
  std::string line;
  bool in_vendor = false;
  std::string vendor_name;
  while (std::getline(in, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (line.front() != '\t') {
      in_vendor = line.size() > 5 && line.compare(0, 4, vendor_key) == 0 && line[4] == ' ';
      if (in_vendor) {
        vendor_name = trim_copy(line.substr(5));
      }
      continue;
    }
    if (!in_vendor || (line.size() > 1 && line[1] == '\t')) {
      continue;
    }
    if (line.size() > 6 && line.compare(1, 4, device_key) == 0) {
      const std::string name = trim_copy(line.substr(6));
      if (!name.empty()) {
        return name;
      }
    }
  }
  return vendor_name;
}

HostFacts read_host_facts() {
  HostFacts facts;
  const OsIdentity os = parse_os_release(read_file("/etc/os-release"));
  facts.lcos = os_identity_is_lcos(os);
  facts.kicker = os.kicker;
  facts.version_line = os.version_line.empty() ? "Unknown system" : os.version_line;

  char host[256] = {};
  if (gethostname(host, sizeof(host) - 1) == 0 && host[0] != '\0') {
    facts.hostname = host;
  }

  const ProcessorInfo cpu = parse_cpuinfo(read_file("/proc/cpuinfo"));
  facts.processor = cpu.label.empty() ? "Unavailable" : cpu.label;

  const long long kib = meminfo_total_kib(read_file("/proc/meminfo"));
  facts.memory = kib > 0 ? format_gigabytes(kib * 1024) : "Unavailable";

  std::string pci_ids = read_file("/usr/share/misc/pci.ids");
  if (pci_ids.empty()) {
    pci_ids = read_file("/usr/share/hwdata/pci.ids");
  }
  std::string graphics;
  std::error_code error;
  const fs::path drm("/sys/class/drm");
  if (fs::is_directory(drm, error)) {
    for (const auto& item : fs::directory_iterator(drm, error)) {
      error.clear();
      const std::string name = item.path().filename().string();
      if (name.compare(0, 4, "card") != 0 || name.find('-') != std::string::npos) {
        continue;
      }
      const fs::path device = item.path() / "device";
      std::string driver;
      std::error_code link_error;
      const fs::path driver_link = fs::read_symlink(device / "driver", link_error);
      if (!link_error) {
        driver = driver_link.filename().string();
      }
      if (driver == "efi-framebuffer" || driver == "simple-framebuffer" || driver == "vesa") {
        continue;
      }
      unsigned vendor = 0;
      unsigned device_id = 0;
      const std::string vendor_text = trim_copy(read_file(device / "vendor"));
      const std::string device_text = trim_copy(read_file(device / "device"));
      std::string label;
      if (!vendor_text.empty() && vendor_text[0] == '0' &&
          hex_byte(vendor_text.substr(vendor_text[1] == 'x' ? 2 : 0), vendor) &&
          !device_text.empty() && device_text[0] == '0' &&
          hex_byte(device_text.substr(device_text[1] == 'x' ? 2 : 0), device_id)) {
        label = pci_device_name(pci_ids, vendor, device_id);
      }
      if (label.empty() && !driver.empty()) {
        label = driver;
      }
      if (label.empty()) {
        continue;
      }
      if (!graphics.empty()) {
        graphics += ", ";
      }
      graphics += label;
    }
  }
  facts.graphics = graphics.empty() ? "Unavailable" : graphics;

  unsigned long long disk_bytes = 0;
  const fs::path block("/sys/block");
  if (fs::is_directory(block, error)) {
    for (const auto& item : fs::directory_iterator(block, error)) {
      error.clear();
      const std::string name = item.path().filename().string();
      if (name.compare(0, 4, "loop") == 0 || name.compare(0, 3, "ram") == 0 ||
          name.compare(0, 2, "fd") == 0 || name.compare(0, 2, "sr") == 0 ||
          name.compare(0, 4, "zram") == 0) {
        continue;
      }
      const std::string size_text = trim_copy(read_file(item.path() / "size"));
      if (size_text.empty()) {
        continue;
      }
      try {
        const unsigned long long sectors = std::stoull(size_text);
        disk_bytes = std::max(disk_bytes, sectors * 512ULL);
      } catch (const std::exception&) {
      }
    }
  }
  struct statvfs fsinfo {};
  long long free_bytes = 0;
  long long fs_bytes = 0;
  if (statvfs("/", &fsinfo) == 0) {
    fs_bytes = static_cast<long long>(fsinfo.f_blocks) * static_cast<long long>(fsinfo.f_frsize);
    free_bytes = static_cast<long long>(fsinfo.f_bavail) * static_cast<long long>(fsinfo.f_frsize);
  }
  const long long total = disk_bytes > 0 ? static_cast<long long>(disk_bytes) : fs_bytes;
  if (total > 0) {
    facts.storage = format_gigabytes(total) + " · " + format_gigabytes(free_bytes) + " free";
  } else {
    facts.storage = "Unavailable";
  }
  return facts;
}

}  // namespace lcos
