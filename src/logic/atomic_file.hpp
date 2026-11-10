// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_ATOMIC_FILE_HPP
#define LCOS_LOGIC_ATOMIC_FILE_HPP

#include <string>
#include <sys/types.h>

namespace lcos {

// How to replace one regular file without following a symlink at the
// destination name. The temporary file is created with O_EXCL|O_NOFOLLOW in
// the same directory, metadata is applied with fchown/fchmod on that fd, and
// renameat swaps it into place.
struct ReplaceRequest {
  mode_t mode_if_new = 0644;
  // When set, the new inode is owned by uid:gid instead of copying the
  // previous owner. Used for ~/.face, which must belong to the account.
  bool force_owner = false;
  uid_t uid = 0;
  gid_t gid = 0;
};

// Refuses when the destination name is a symlink or any other non-regular
// file. An existing regular file keeps its owner, group, and mode unless
// force_owner is set. Returns false and writes a reason on failure. The
// previous file is left unchanged when the replacement cannot be finished.
bool replace_regular_file(const std::string& path, const std::string& data,
                          const ReplaceRequest& request, std::string* error);

}  // namespace lcos

#endif
