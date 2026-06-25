#pragma once

#include <string>
#include <vector>

#include "core/Region.h"

namespace video_engine::timeline {

struct TextBoxSpec {
  std::string srt_path;
  std::string text;
  std::string font_family = "Noto Sans";
  std::string font_path;
  std::string text_color = "#FFF200";
  std::string outline_color = "#101010";
  std::string back_color = "#00000000";
  float font_size_percent = 1.5f;
  int outline = 3;
  int shadow = 0;
  bool bold = true;
  bool italic = true;
  bool uppercase = false;
  bool wrap = true;
  bool clip = true;
  bool auto_fit = true;
  int padding_x = 0;
  int padding_y = 0;
  std::string align_h = "center";
  std::string align_v = "middle";
  float opacity = 1.0f;
  std::vector<Region> regions;
};

struct BlurBoxSpec {
  bool enabled = true;
  std::vector<Region> regions;
};

struct WatermarkSpec {
  bool enabled = false;
};

}  // namespace video_engine::timeline
