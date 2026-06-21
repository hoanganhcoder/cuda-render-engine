#pragma once

#include <string>
#include <vector>

#include <pybind11/pybind11.h>

#include "core/Region.h"

namespace video_engine {

struct RenderJob {
  std::string input;
  std::string output;
  int width = 0;
  int height = 0;
  double fps = 0.0;
  std::string subtitle_srt;
  std::string subtitle_text;
  std::string subtitle_font_family = "Noto Sans";
  std::string subtitle_font_path;
  std::string subtitle_text_color = "#FFF200";
  std::string subtitle_outline_color = "#101010";
  std::string subtitle_back_color = "#00000000";
  int subtitle_font_scale = 4;
  int subtitle_font_size = 36;
  int subtitle_margin = 8;
  int subtitle_outline = 3;
  int subtitle_shadow = 0;
  bool subtitle_bold = true;
  bool subtitle_italic = false;
  float subtitle_opacity = 1.0f;
  std::vector<Region> regions;

  static RenderJob fromPythonDict(const pybind11::dict& job_dict);
  void validate() const;
};

}  // namespace video_engine
