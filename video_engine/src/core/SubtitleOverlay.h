#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace video_engine {

struct SubtitleOverlay {
  bool enabled = false;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int stride = 0;
  uint8_t luma = 235;
  uint8_t chroma_u = 128;
  uint8_t chroma_v = 128;
  float opacity = 1.0f;
  std::string cue_text;
  std::vector<uint8_t> alpha_mask;
};

}  // namespace video_engine
