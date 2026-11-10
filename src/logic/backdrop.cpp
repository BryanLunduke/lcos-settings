// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/backdrop.hpp"

#include <cctype>

namespace lcos {

std::string backdrop_property(int screen, const std::string& monitor, int workspace,
                              const std::string& leaf) {
  const std::string path = "/backdrop/screen" + std::to_string(screen) + "/monitor" +
                           monitor + "/workspace" + std::to_string(workspace) + "/" + leaf;
  std::string out;
  out.reserve(path.size());
  for (char character : path) {
    if (character != ' ' && character != '\t') {
      out.push_back(character);
    }
  }
  return out;
}

bool is_backdrop_image(const std::string& filename) {
  const auto dot = filename.rfind('.');
  if (dot == std::string::npos || dot + 1 >= filename.size()) {
    return false;
  }
  std::string ext = filename.substr(dot + 1);
  for (char& character : ext) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "gif" ||
         ext == "webp" || ext == "tif" || ext == "tiff" || ext == "svg";
}

}  // namespace lcos
