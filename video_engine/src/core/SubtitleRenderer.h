#pragma once

#include <string>

#include "core/Region.h"
#include "core/SubtitleOverlay.h"

namespace video_engine {

class SubtitleRenderer {
public:
  static SubtitleOverlay buildOverlay(
      const Region& region,
      const std::string& text,
      int video_width,
      int video_height,
      int font_scale,
      int margin,
      float opacity);
};

}  // namespace video_engine
