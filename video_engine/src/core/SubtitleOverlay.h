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
  float opacity = 1.0f;
  std::string cue_text;
  std::vector<uint8_t> alpha_mask;
  std::vector<uint8_t> luma_mask;
  std::vector<uint8_t> chroma_u_mask;
  std::vector<uint8_t> chroma_v_mask;
};

}  // namespace video_engine
