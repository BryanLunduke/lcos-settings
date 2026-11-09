// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_XRANDR_BACKEND_HPP
#define LCOS_XRANDR_BACKEND_HPP

#include "logic/display_layout.hpp"

#include <string>
#include <vector>

typedef struct _XDisplay Display;

namespace lcos {

// Live RandR state. Applying a layout writes the CRTCs directly and is what
// the confirm dialog reverts. xfsettingsd is asked to do the same work by
// setting /Schemes/Apply on the displays channel.
class XrandrBackend {
 public:
  explicit XrandrBackend(Display* display);
  bool available() const { return display_ != nullptr && ready_; }
  const std::string& error() const { return error_; }

  std::vector<OutputConfig> read();
  // Empty string on success, otherwise a reason the configuration was rejected.
  std::string apply(const std::vector<OutputConfig>& desired);

 private:
  Display* display_ = nullptr;
  bool ready_ = false;
  std::string error_;
};

}  // namespace lcos

#endif
