// SPDX-License-Identifier: GPL-3.0-or-later

#include "atomic_file.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace lcos {
namespace {

std::string errno_text(const std::string& prefix) {
  return prefix + ": " + std::strerror(errno);
}

std::string random_name() {
  static std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<unsigned> dist(0, 15);
  std::string name = ".lcos-";
  for (int i = 0; i < 12; ++i) {
    const unsigned value = dist(rng);
    name.push_back(static_cast<char>(value < 10 ? '0' + value : 'a' + (value - 10)));
  }
  return name;
}

bool write_all(int fd, const std::string& data, std::string* error) {
  std::size_t wrote = 0;
  while (wrote < data.size()) {
    const ssize_t n = ::write(fd, data.data() + wrote, data.size() - wrote);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (error != nullptr) {
        *error = errno_text("could not write the new file");
      }
      return false;
    }
    wrote += static_cast<std::size_t>(n);
  }
  return true;
}

}  // namespace

bool replace_regular_file(const std::string& path, const std::string& data,
                          const ReplaceRequest& request, std::string* error) {
  const auto slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0 || slash + 1 >= path.size()) {
    if (error != nullptr) {
      *error = "refusing a relative or empty path";
    }
    return false;
  }
  const std::string dir = path.substr(0, slash);
  const std::string name = path.substr(slash + 1);
  if (name == "." || name == ".." || name.find('/') != std::string::npos) {
    if (error != nullptr) {
      *error = "refusing an unsafe file name";
    }
    return false;
  }

  const int dirfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (dirfd < 0) {
    if (error != nullptr) {
      *error = errno_text("could not open " + dir);
    }
    return false;
  }

  struct stat st;
  const bool exists = ::fstatat(dirfd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0;
  if (!exists && errno != ENOENT) {
    if (error != nullptr) {
      *error = errno_text("could not inspect " + path);
    }
    ::close(dirfd);
    return false;
  }
  if (exists && !S_ISREG(st.st_mode)) {
    if (error != nullptr) {
      *error = "refusing to replace a non-regular file";
    }
    ::close(dirfd);
    return false;
  }

  int fd = -1;
  std::string tmpname;
  for (int attempt = 0; attempt < 16 && fd < 0; ++attempt) {
    tmpname = random_name();
    fd = ::openat(dirfd, tmpname.c_str(), O_CREAT | O_EXCL | O_NOFOLLOW | O_WRONLY, 0600);
    if (fd < 0 && errno != EEXIST) {
      if (error != nullptr) {
        *error = errno_text("could not create a temporary file in " + dir);
      }
      ::close(dirfd);
      return false;
    }
  }
  if (fd < 0) {
    if (error != nullptr) {
      *error = "could not create a temporary file in " + dir;
    }
    ::close(dirfd);
    return false;
  }

  bool ok = write_all(fd, data, error);
  if (ok && ::fsync(fd) != 0) {
    if (error != nullptr) {
      *error = errno_text("could not store the new file");
    }
    ok = false;
  }

  const uid_t uid = request.force_owner ? request.uid : (exists ? st.st_uid : ::geteuid());
  const gid_t gid = request.force_owner ? request.gid : (exists ? st.st_gid : ::getegid());
  const mode_t mode = request.force_owner || !exists ? request.mode_if_new : (st.st_mode & 07777);
  if (ok && ::fchown(fd, uid, gid) != 0) {
    // A non-root caller cannot change ownership. Keeping the current user is
    // enough when the file is already theirs. Root must be able to preserve
    // root:shadow and to hand ~/.face to the account.
    if (!(errno == EPERM && uid == ::geteuid() && gid == ::getegid())) {
      if (error != nullptr) {
        *error = errno_text("could not set ownership of " + path);
      }
      ok = false;
    }
  }
  if (ok && ::fchmod(fd, mode) != 0) {
    if (error != nullptr) {
      *error = errno_text("could not set permissions of " + path);
    }
    ok = false;
  }

  // Re-check the destination after the bytes are durable. renameat replaces a
  // symlink name; it does not follow it. Refuse if a symlink appeared.
  struct stat again;
  if (ok && ::fstatat(dirfd, name.c_str(), &again, AT_SYMLINK_NOFOLLOW) == 0 && !S_ISREG(again.st_mode)) {
    if (error != nullptr) {
      *error = "refusing to replace a non-regular file";
    }
    ok = false;
  }
  if (ok && ::renameat(dirfd, tmpname.c_str(), dirfd, name.c_str()) != 0) {
    if (error != nullptr) {
      *error = errno_text("could not replace " + path);
    }
    ok = false;
  }
  if (!ok) {
    ::unlinkat(dirfd, tmpname.c_str(), 0);
  }
  ::close(fd);
  ::close(dirfd);
  return ok;
}

}  // namespace lcos
