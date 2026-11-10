// SPDX-License-Identifier: GPL-3.0-or-later

#include "pointer_backend.hpp"

#include "logic/names.hpp"

#include <X11/Xlib.h>
#include <X11/extensions/XInput.h>

#include <algorithm>
#include <cctype>

namespace lcos {
namespace {

bool contains_ci(const std::string& text, const char* needle) {
  std::string hay = text;
  std::string pin = needle;
  std::transform(hay.begin(), hay.end(), hay.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::transform(pin.begin(), pin.end(), pin.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return hay.find(pin) != std::string::npos;
}

}  // namespace

bool PointerDevice::has_atom(const std::string& atom) const {
  return std::find(atoms.begin(), atoms.end(), atom) != atoms.end();
}

PointerBackend::PointerBackend(Display* display) : display_(display) {}

std::vector<PointerDevice> PointerBackend::list() const {
  std::vector<PointerDevice> devices;
  if (display_ == nullptr) {
    return devices;
  }
  int count = 0;
  auto previous = XSetErrorHandler([](Display*, XErrorEvent*) { return 0; });
  XDeviceInfo* infos = XListInputDevices(display_, &count);
  if (infos == nullptr) {
    XSetErrorHandler(previous);
    return devices;
  }
  for (int i = 0; i < count; ++i) {
    const XDeviceInfo& info = infos[i];
    if (info.name == nullptr) {
      continue;
    }
    if (info.use != IsXExtensionPointer && info.use != IsXPointer) {
      continue;
    }
    const std::string name = info.name;
    if (contains_ci(name, "XTEST")) {
      continue;
    }
    PointerDevice device;
    device.id = info.id;
    device.name = name;
    device.xfconf_name = device_xfconf_name(name);
    XDevice* opened = XOpenDevice(display_, info.id);
    if (opened != nullptr) {
      int nprops = 0;
      Atom* props = XListDeviceProperties(display_, opened, &nprops);
      if (props != nullptr) {
        for (int p = 0; p < nprops; ++p) {
          char* atom = XGetAtomName(display_, props[p]);
          if (atom != nullptr) {
            device.atoms.emplace_back(atom);
            XFree(atom);
          }
        }
        XFree(props);
      }
      XCloseDevice(display_, opened);
    }
    device.touchpad = contains_ci(name, "touchpad") || contains_ci(name, "synaptics") ||
                      contains_ci(name, "trackpad") ||
                      device.has_atom("libinput Tapping Enabled") ||
                      device.has_atom("Synaptics Tap Action");
    devices.push_back(std::move(device));
  }
  XFreeDeviceList(infos);
  XSetErrorHandler(previous);
  return devices;
}

}  // namespace lcos
