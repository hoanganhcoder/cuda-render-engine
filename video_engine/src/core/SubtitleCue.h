#pragma once

#include <string>

namespace video_engine {

struct SubtitleCue {
  double start = 0.0;
  double end = 0.0;
  std::string text;

  [[nodiscard]] bool isActive(double timestamp) const {
    return timestamp >= start && timestamp <= end;
  }
};

}  // namespace video_engine
