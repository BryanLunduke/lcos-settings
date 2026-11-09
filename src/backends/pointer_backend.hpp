// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_POINTER_BACKEND_HPP
#define LCOS_POINTER_BACKEND_HPP

#include <string>
#include <vector>

typedef struct _XDisplay Display;

namespace lcos {

struct PointerDevice {
  unsigned long id = 0;
  std::string name;
  std::string xfconf_name;
  bool touchpad = false;
  std::vector<std::string> atoms;

  bool has_atom(const std::string& atom) const;
};

// Lists pointer devices and the XInput properties they currently expose.
// Settings themselves are stored in the pointers xfconf channel; xfsettingsd
// applies them. This list is how the page decides which controls apply.
class PointerBackend {
 public:
  explicit PointerBackend(Display* display);
  std::vector<PointerDevice> list() const;

 private:
  Display* display_ = nullptr;
};

}  // namespace lcos

#endif
