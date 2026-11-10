// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_DISPLAY_LAYOUT_HPP
#define LCOS_LOGIC_DISPLAY_LAYOUT_HPP

#include <string>
#include <vector>

namespace lcos {

struct ModeInfo {
  int width = 0;
  int height = 0;
  double refresh = 0;
  unsigned long id = 0;
};

// One connected RandR output, in the same units xfce4-display-settings stores.
struct OutputConfig {
  std::string name;
  std::string friendly;
  std::string edid;
  bool active = true;
  bool primary = false;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  double refresh = 0;
  int rotation = 0;              // 0, 90, 180, 270
  std::string reflection = "0";  // 0, X, Y, XY
  double scale = 1.0;
  std::vector<ModeInfo> modes;
};

struct ScreenBounds {
  int width = 1;
  int height = 1;
};

bool parse_resolution(const std::string& text, int& width, int& height);
std::string format_resolution(int width, int height);
bool refresh_matches(double a, double b);
double mode_refresh_hz(unsigned long dot_clock, unsigned int h_total,
                       unsigned int v_total);

int rotated_width(const OutputConfig& output);
int rotated_height(const OutputConfig& output);
int footprint_width(const OutputConfig& output);
int footprint_height(const OutputConfig& output);

// xfconf displays/<output>/Scale is the RandR scalex. UI scale 2 is stored
// as 0.5, which is what xfsettingsd writes into the CRTC transform.
// xfce4-display-settings 4.20 uses the same reciprocal.
double xfconf_scale_from_ui(double ui_scale);
double ui_scale_from_xfconf(double stored);

ScreenBounds screen_bounds(const std::vector<OutputConfig>& outputs);
void normalize_positions(std::vector<OutputConfig>& outputs);
void arrange_extend(std::vector<OutputConfig>& outputs);
void arrange_mirror(std::vector<OutputConfig>& outputs);

const ModeInfo* find_mode(const std::vector<ModeInfo>& modes, int width,
                          int height, double refresh);
double closest_refresh(const OutputConfig& output, int width, int height,
                       double preferred);

std::string friendly_output_name(const std::string& randr_name,
                                 const std::string& edid_name);

// Millimetres for a 96 DPI framebuffer, matching xfsettingsd.
int millimeters_for_pixels(int pixels);

}  // namespace lcos

#endif
