// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_LOGIC_VOLUME_HPP
#define LCOS_LOGIC_VOLUME_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lcos {

// PA_VOLUME_NORM. Kept here so the conversion can be tested without libpulse.
inline constexpr std::uint32_t kPaVolumeNorm = 65536;
inline constexpr int kVolumePercentMax = 150;

inline int volume_to_percent(std::uint32_t volume) {
  return static_cast<int>(
      std::lround(100.0 * static_cast<double>(volume) / kPaVolumeNorm));
}

inline std::uint32_t percent_to_volume(int percent) {
  percent = std::clamp(percent, 0, kVolumePercentMax);
  return static_cast<std::uint32_t>(
      std::lround(static_cast<double>(percent) / 100.0 * kPaVolumeNorm));
}

// Move every channel so the loudest one matches `percent`, keeping the
// ratio between channels. That is what preserves balance.
inline std::vector<std::uint32_t> scale_channel_volumes(const std::vector<std::uint32_t>& channels,
                                                       int percent) {
  const std::uint32_t target = percent_to_volume(percent);
  if (channels.empty()) {
    return {target};
  }
  std::uint32_t loudest = 0;
  for (const std::uint32_t volume : channels) {
    loudest = std::max(loudest, volume);
  }
  if (loudest == 0) {
    return std::vector<std::uint32_t>(channels.size(), target);
  }
  std::vector<std::uint32_t> scaled;
  scaled.reserve(channels.size());
  for (const std::uint32_t volume : channels) {
    scaled.push_back(static_cast<std::uint32_t>(
        std::lround(static_cast<double>(volume) / static_cast<double>(loudest) * target)));
  }
  return scaled;
}

}  // namespace lcos

#endif
