#pragma once

#include <string>

#include "core/timeline/LayerSpec.h"

namespace video_engine::timeline {

struct Sequence {
  std::string input_path;
  std::string output_path;
  int width = 0;
  int height = 0;
  double fps = 0.0;
  float video_scale = 1.0f;
  bool flip_horizontal = false;
  TextBoxSpec subtitle;
  BlurBoxSpec blur_box;
  WatermarkSpec watermark;
};

}  // namespace video_engine::timeline
