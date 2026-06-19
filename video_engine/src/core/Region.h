#pragma once

#include <algorithm>

namespace video_engine {

struct Region {
  double start = 0.0;
  double end = 0.0;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  float strength = 0.0f;
  float feather = 0.0f;
  float vertical_stretch = 1.0f;
  float horizontal_blur = 0.0f;
  float temporal_blend = 0.0f;

  [[nodiscard]] bool isActive(double timestamp) const {
    return timestamp >= start && timestamp <= end;
  }

  void clampToBounds(int width, int height) {
    x = std::clamp(x, 0, width);
    y = std::clamp(y, 0, height);
    w = std::clamp(w, 0, width - x);
    h = std::clamp(h, 0, height - y);
    strength = std::clamp(strength, 0.0f, 1.0f);
    feather = std::max(feather, 0.0f);
    vertical_stretch = std::clamp(vertical_stretch, 0.0f, 2.0f);
    horizontal_blur = std::clamp(horizontal_blur, 0.0f, 1.0f);
    temporal_blend = std::clamp(temporal_blend, 0.0f, 1.0f);
    if (end < start) {
      end = start;
    }
  }
};

}  // namespace video_engine
