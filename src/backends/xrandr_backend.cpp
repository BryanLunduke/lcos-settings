// SPDX-License-Identifier: GPL-3.0-or-later

#include "xrandr_backend.hpp"

#include "logic/names.hpp"

#include <glib.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace lcos {
namespace {

int g_x_error = 0;

int error_trap(Display*, XErrorEvent* event) {
  g_x_error = event->error_code;
  return 0;
}

bool scale_differs(double left, double right) {
  return std::abs(left - right) > 0.01;
}

void set_crtc_matrix(Display* display, RRCrtc crtc, double matrix) {
  XTransform transform;
  std::memset(&transform, 0, sizeof(transform));
  transform.matrix[0][0] = XDoubleToFixed(matrix);
  transform.matrix[1][1] = XDoubleToFixed(matrix);
  transform.matrix[2][2] = XDoubleToFixed(1.0);
  // libXrandr calls strlen on the filter. NULL crashes (Revert and scale 1
  // both did). xfsettingsd 4.20 uses "nearest" for an identity transform and
  // "bilinear" otherwise. The call is skipped when the matrix is already
  // identity, so Xvfb's BadValue on XRRSetCrtcTransform stays off the
  // session test.
  const char* filter = scale_differs(matrix, 1.0) ? "bilinear" : "nearest";
  XRRSetCrtcTransform(display, crtc, &transform, filter, nullptr, 0);
  XSync(display, False);
}

std::string edid_checksum(Display* display, RROutput output, const XRROutputInfo* info) {
  Atom edid_atom = XInternAtom(display, "EDID", True);
  if (edid_atom != None) {
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long count = 0;
    unsigned long after = 0;
    unsigned char* data = nullptr;
    if (XRRGetOutputProperty(display, output, edid_atom, 0, 100, False, False,
                             AnyPropertyType, &actual_type, &actual_format, &count,
                             &after, &data) == Success &&
        data != nullptr && actual_format == 8 && count >= 128) {
      gchar* sum = g_compute_checksum_for_data(G_CHECKSUM_SHA1, data, 128);
      std::string hex = sum != nullptr ? sum : "";
      g_free(sum);
      XFree(data);
      data = nullptr;
      if (!hex.empty()) {
        return hex;
      }
    }
    if (data != nullptr) {
      XFree(data);
    }
  }
  const std::string fallback =
      std::string(info->name != nullptr ? info->name : "output") + "-" +
      std::to_string(info->mm_width) + "-" + std::to_string(info->mm_height) +
      "-" + std::to_string(info->nmode);
  gchar* sum = g_compute_checksum_for_string(G_CHECKSUM_SHA1, fallback.c_str(), -1);
  std::string hex = sum != nullptr ? sum : "";
  g_free(sum);
  return hex;
}

Rotation rotation_bits(const OutputConfig& output) {
  Rotation rotation = RR_Rotate_0;
  switch (output.rotation) {
    case 90:
      rotation = RR_Rotate_90;
      break;
    case 180:
      rotation = RR_Rotate_180;
      break;
    case 270:
      rotation = RR_Rotate_270;
      break;
    default:
      rotation = RR_Rotate_0;
      break;
  }
  if (output.reflection == "X") {
    rotation |= RR_Reflect_X;
  } else if (output.reflection == "Y") {
    rotation |= RR_Reflect_Y;
  } else if (output.reflection == "XY") {
    rotation |= RR_Reflect_X | RR_Reflect_Y;
  }
  return rotation;
}

void decode_rotation(Rotation rotation, int& degrees, std::string& reflection) {
  if (rotation & RR_Rotate_90) {
    degrees = 90;
  } else if (rotation & RR_Rotate_180) {
    degrees = 180;
  } else if (rotation & RR_Rotate_270) {
    degrees = 270;
  } else {
    degrees = 0;
  }
  const bool reflect_x = (rotation & RR_Reflect_X) != 0;
  const bool reflect_y = (rotation & RR_Reflect_Y) != 0;
  if (reflect_x && reflect_y) {
    reflection = "XY";
  } else if (reflect_x) {
    reflection = "X";
  } else if (reflect_y) {
    reflection = "Y";
  } else {
    reflection = "0";
  }
}

}  // namespace

XrandrBackend::XrandrBackend(Display* display) : display_(display) {
  if (display_ == nullptr) {
    error_ = "No X display";
    return;
  }
  int error_base = 0;
  if (!XRRQueryExtension(display_, &event_base_, &error_base)) {
    error_ = "This X server has no RandR extension";
    return;
  }
  ready_ = true;
}

void XrandrBackend::select_screen_events() {
  if (!ready_ || display_ == nullptr) {
    return;
  }
  XRRSelectInput(display_, RootWindow(display_, DefaultScreen(display_)), RRScreenChangeNotifyMask);
}

bool XrandrBackend::is_screen_change(const void* xevent) const {
  if (!ready_ || xevent == nullptr) {
    return false;
  }
  const auto* event = static_cast<const XEvent*>(xevent);
  return event->type == event_base_ + RRScreenChangeNotify;
}

std::vector<OutputConfig> XrandrBackend::read() {
  std::vector<OutputConfig> outputs;
  if (!ready_) {
    return outputs;
  }
  Window root = RootWindow(display_, DefaultScreen(display_));
  XRRScreenResources* resources = XRRGetScreenResources(display_, root);
  if (resources == nullptr) {
    error_ = "Could not read screen resources";
    return outputs;
  }
  RROutput primary = XRRGetOutputPrimary(display_, root);
  std::map<RRCrtc, XRRCrtcInfo*> crtcs;
  for (int i = 0; i < resources->ncrtc; ++i) {
    XRRCrtcInfo* info = XRRGetCrtcInfo(display_, resources, resources->crtcs[i]);
    if (info != nullptr) {
      crtcs[resources->crtcs[i]] = info;
    }
  }

  for (int i = 0; i < resources->noutput; ++i) {
    XRROutputInfo* info = XRRGetOutputInfo(display_, resources, resources->outputs[i]);
    if (info == nullptr) {
      continue;
    }
    if (info->connection != RR_Connected || info->name == nullptr) {
      XRRFreeOutputInfo(info);
      continue;
    }
    OutputConfig output;
    output.name = output_xfconf_name(info->name);
    output.edid = edid_checksum(display_, resources->outputs[i], info);
    output.friendly = friendly_output_name(info->name, "");
    output.primary = resources->outputs[i] == primary;
    output.scale = 1.0;

    for (int m = 0; m < info->nmode; ++m) {
      for (int k = 0; k < resources->nmode; ++k) {
        const XRRModeInfo& mode = resources->modes[k];
        if (mode.id != info->modes[m]) {
          continue;
        }
        ModeInfo stored;
        stored.id = mode.id;
        stored.width = static_cast<int>(mode.width);
        stored.height = static_cast<int>(mode.height);
        stored.refresh = mode_refresh_hz(mode.dotClock, mode.hTotal, mode.vTotal);
        output.modes.push_back(stored);
      }
    }

    XRRCrtcInfo* crtc = nullptr;
    if (info->crtc != None) {
      const auto found = crtcs.find(info->crtc);
      if (found != crtcs.end()) {
        crtc = found->second;
      }
    }
    if (crtc != nullptr && crtc->mode != None) {
      output.active = true;
      output.x = crtc->x;
      output.y = crtc->y;
      decode_rotation(crtc->rotation, output.rotation, output.reflection);
      const ModeInfo* mode = nullptr;
      for (const auto& candidate : output.modes) {
        if (candidate.id == crtc->mode) {
          mode = &candidate;
          break;
        }
      }
      if (mode != nullptr) {
        output.width = mode->width;
        output.height = mode->height;
        output.refresh = mode->refresh;
      } else {
        output.width = static_cast<int>(crtc->width);
        output.height = static_cast<int>(crtc->height);
      }
      XRRCrtcTransformAttributes* transform = nullptr;
      if (XRRGetCrtcTransform(display_, info->crtc, &transform) && transform != nullptr) {
        // The RandR matrix is 1/ui-scale, matching xfce4-settings 4.20.
        const double matrix = XFixedToDouble(transform->currentTransform.matrix[0][0]);
        output.scale = matrix > 0.05 ? 1.0 / matrix : 1.0;
        XFree(transform);
      }
    } else if (!output.modes.empty()) {
      output.active = false;
      output.width = output.modes.front().width;
      output.height = output.modes.front().height;
      output.refresh = output.modes.front().refresh;
    } else {
      XRRFreeOutputInfo(info);
      continue;
    }
    outputs.push_back(std::move(output));
    XRRFreeOutputInfo(info);
  }

  for (auto& entry : crtcs) {
    XRRFreeCrtcInfo(entry.second);
  }
  XRRFreeScreenResources(resources);

  if (outputs.size() == 1) {
    outputs.front().primary = true;
  } else {
    bool any_primary = false;
    for (const auto& output : outputs) {
      any_primary = any_primary || output.primary;
    }
    if (!any_primary && !outputs.empty()) {
      outputs.front().primary = true;
    }
  }
  return outputs;
}

std::string XrandrBackend::apply(const std::vector<OutputConfig>& desired) {
  if (!ready_) {
    return error_.empty() ? "RandR is not available" : error_;
  }
  std::vector<OutputConfig> layout = desired;
  normalize_positions(layout);
  int active_count = 0;
  for (const auto& output : layout) {
    if (output.active) {
      ++active_count;
    }
  }
  if (active_count == 0) {
    return "At least one display has to stay on";
  }

  Window root = RootWindow(display_, DefaultScreen(display_));
  XRRScreenResources* resources = XRRGetScreenResources(display_, root);
  if (resources == nullptr) {
    return "Could not read screen resources";
  }

  struct Assignment {
    const OutputConfig* config = nullptr;
    RROutput output = None;
    RRCrtc crtc = None;
    RRMode mode = None;
  };
  std::vector<Assignment> plan;
  std::set<RRCrtc> used;

  for (const auto& output : layout) {
    XRROutputInfo* info = nullptr;
    RROutput id = None;
    for (int i = 0; i < resources->noutput; ++i) {
      XRROutputInfo* candidate =
          XRRGetOutputInfo(display_, resources, resources->outputs[i]);
      if (candidate == nullptr) {
        continue;
      }
      const std::string name =
          candidate->name != nullptr ? output_xfconf_name(candidate->name) : "";
      if (name == output.name) {
        info = candidate;
        id = resources->outputs[i];
        break;
      }
      XRRFreeOutputInfo(candidate);
    }
    if (info == nullptr) {
      XRRFreeScreenResources(resources);
      return "Display " + output.name + " is no longer connected";
    }
    Assignment assignment;
    assignment.config = &output;
    assignment.output = id;
    if (output.active) {
      std::vector<ModeInfo> modes;
      for (int m = 0; m < info->nmode; ++m) {
        for (int k = 0; k < resources->nmode; ++k) {
          const XRRModeInfo& mode = resources->modes[k];
          if (mode.id != info->modes[m]) {
            continue;
          }
          ModeInfo stored;
          stored.id = mode.id;
          stored.width = static_cast<int>(mode.width);
          stored.height = static_cast<int>(mode.height);
          stored.refresh = mode_refresh_hz(mode.dotClock, mode.hTotal, mode.vTotal);
          modes.push_back(stored);
        }
      }
      const ModeInfo* match =
          find_mode(modes, output.width, output.height, output.refresh);
      if (match == nullptr) {
        const std::string message = "No mode " + format_resolution(output.width, output.height) +
                                    " for " + output.name;
        XRRFreeOutputInfo(info);
        XRRFreeScreenResources(resources);
        return message;
      }
      assignment.mode = match->id;
      for (int c = 0; c < info->ncrtc; ++c) {
        if (used.count(info->crtcs[c]) == 0) {
          assignment.crtc = info->crtcs[c];
          used.insert(assignment.crtc);
          break;
        }
      }
      if (assignment.crtc == None && info->crtc != None && used.count(info->crtc) == 0) {
        assignment.crtc = info->crtc;
        used.insert(assignment.crtc);
      }
      if (assignment.crtc == None) {
        XRRFreeOutputInfo(info);
        XRRFreeScreenResources(resources);
        return "No CRTC available for " + output.name;
      }
    }
    plan.push_back(assignment);
    XRRFreeOutputInfo(info);
  }

  const ScreenBounds bounds = screen_bounds(layout);
  g_x_error = 0;
  XErrorHandler previous = XSetErrorHandler(error_trap);
  XGrabServer(display_);

  // Snapshot every CRTC before changing anything. The new layout is already
  // validated above, so a rejection from the server can be rolled back
  // instead of leaving every output disabled.
  struct CrtcSnap {
    RRCrtc crtc = None;
    int x = 0;
    int y = 0;
    RRMode mode = None;
    Rotation rotation = RR_Rotate_0;
    double matrix = 1.0;
    bool has_transform = false;
    std::vector<RROutput> outputs;
  };
  std::vector<CrtcSnap> saved;
  saved.reserve(static_cast<std::size_t>(resources->ncrtc));
  for (int i = 0; i < resources->ncrtc; ++i) {
    XRRCrtcInfo* info = XRRGetCrtcInfo(display_, resources, resources->crtcs[i]);
    if (info == nullptr) {
      continue;
    }
    CrtcSnap snap;
    snap.crtc = resources->crtcs[i];
    snap.x = info->x;
    snap.y = info->y;
    snap.mode = info->mode;
    snap.rotation = info->rotation;
    snap.outputs.assign(info->outputs, info->outputs + info->noutput);
    XRRCrtcTransformAttributes* transform = nullptr;
    if (XRRGetCrtcTransform(display_, snap.crtc, &transform) && transform != nullptr) {
      snap.matrix = XFixedToDouble(transform->currentTransform.matrix[0][0]);
      if (snap.matrix < 0.05) {
        snap.matrix = 1.0;
      }
      snap.has_transform = true;
      XFree(transform);
    }
    saved.push_back(std::move(snap));
    XRRFreeCrtcInfo(info);
  }
  const int old_width = DisplayWidth(display_, DefaultScreen(display_));
  const int old_height = DisplayHeight(display_, DefaultScreen(display_));
  auto restore = [&]() {
    const int wide = std::max(old_width, bounds.width);
    const int high = std::max(old_height, bounds.height);
    XRRSetScreenSize(display_, root, wide, high, millimeters_for_pixels(wide),
                     millimeters_for_pixels(high));
    for (const auto& snap : saved) {
      // SetCrtcConfig BadMatches when a leftover scale transform does not
      // agree with the mode. Put the saved transform back first.
      if (snap.has_transform) {
        set_crtc_matrix(display_, snap.crtc, snap.matrix);
        g_x_error = 0;
      }
      XRRSetCrtcConfig(display_, resources, snap.crtc, CurrentTime, snap.x, snap.y, snap.mode,
                       snap.rotation, snap.outputs.empty() ? nullptr : const_cast<RROutput*>(snap.outputs.data()),
                       static_cast<int>(snap.outputs.size()));
    }
    XRRSetScreenSize(display_, root, old_width, old_height, millimeters_for_pixels(old_width),
                     millimeters_for_pixels(old_height));
  };

  // Grow the screen first so a CRTC can be placed anywhere in the new layout
  // without disabling the outputs that are already on.
  const int wide = std::max(old_width, bounds.width);
  const int high = std::max(old_height, bounds.height);
  XRRSetScreenSize(display_, root, wide, high, millimeters_for_pixels(wide),
                   millimeters_for_pixels(high));

  std::string failure;
  RROutput primary = None;
  for (const auto& assignment : plan) {
    if (!assignment.config->active) {
      continue;
    }
    const double ui_scale = assignment.config->scale > 0.05 ? assignment.config->scale : 1.0;
    const double matrix = 1.0 / ui_scale;
    double current_matrix = 1.0;
    for (const auto& snap : saved) {
      if (snap.crtc == assignment.crtc && snap.has_transform) {
        current_matrix = snap.matrix;
        break;
      }
    }
    // Identity is left alone when it is already identity: Xvfb rejects
    // XRRSetCrtcTransform with BadValue. A scale of 1 after a scale of 2
    // still has to clear the old matrix or SetCrtcConfig returns BadMatch.
    if (scale_differs(current_matrix, matrix)) {
      set_crtc_matrix(display_, assignment.crtc, matrix);
      if (g_x_error != 0 && failure.empty()) {
        failure = "The X server rejected scale " + std::to_string(ui_scale) + " for " +
                  assignment.config->name;
      }
      g_x_error = 0;
    }

    const Rotation rotation = rotation_bits(*assignment.config);
    RROutput output_id = assignment.output;
    const Status status = XRRSetCrtcConfig(
        display_, resources, assignment.crtc, CurrentTime, assignment.config->x,
        assignment.config->y, assignment.mode, rotation, &output_id, 1);
    if (status != RRSetConfigSuccess && failure.empty()) {
      failure = "The X server rejected the layout for " + assignment.config->name +
                " (status " + std::to_string(static_cast<int>(status)) + ")";
    }
    if (assignment.config->primary) {
      primary = assignment.output;
    }
  }
  if (failure.empty()) {
    for (int i = 0; i < resources->ncrtc; ++i) {
      if (used.count(resources->crtcs[i]) != 0) {
        continue;
      }
      XRRSetCrtcConfig(display_, resources, resources->crtcs[i], CurrentTime, 0, 0, None, RR_Rotate_0,
                       nullptr, 0);
    }
    XRRSetScreenSize(display_, root, bounds.width, bounds.height, millimeters_for_pixels(bounds.width),
                     millimeters_for_pixels(bounds.height));
    if (primary == None) {
      for (const auto& assignment : plan) {
        if (assignment.config->active) {
          primary = assignment.output;
          break;
        }
      }
    }
    XRRSetOutputPrimary(display_, root, primary);
  } else {
    restore();
  }
  XUngrabServer(display_);
  XSync(display_, False);
  XSetErrorHandler(previous);
  XRRFreeScreenResources(resources);
  if (g_x_error != 0 && failure.empty()) {
    failure = "The X server reported error " + std::to_string(g_x_error) +
              " while applying the layout";
  }
  return failure;
}

bool XrandrBackend::outputs_have_backlight() const {
  if (!ready_ || display_ == nullptr) {
    return false;
  }
  Window root = DefaultRootWindow(display_);
  XRRScreenResources* resources = XRRGetScreenResources(display_, root);
  if (resources == nullptr) {
    return false;
  }
  bool found = false;
  for (int i = 0; i < resources->noutput && !found; ++i) {
    int count = 0;
    Atom* atoms = XRRListOutputProperties(display_, resources->outputs[i], &count);
    for (int j = 0; j < count; ++j) {
      char* name = XGetAtomName(display_, atoms[j]);
      if (name != nullptr &&
          (std::strcmp(name, "Backlight") == 0 || std::strcmp(name, "BACKLIGHT") == 0)) {
        found = true;
      }
      if (name != nullptr) {
        XFree(name);
      }
    }
    if (atoms != nullptr) {
      XFree(atoms);
    }
  }
  XRRFreeScreenResources(resources);
  return found;
}

}  // namespace lcos
