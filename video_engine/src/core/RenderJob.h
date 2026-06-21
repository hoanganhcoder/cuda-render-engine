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
  std::string logo_path;
  float logo_scale = 0.18f;
  float logo_opacity = 0.22f;
  int logo_margin = 24;
  bool logo_bounce = false;
  float logo_speed_x = 72.0f;
  float logo_speed_y = 48.0f;
  std::string watermark_text;
  std::string watermark_font_family = "Noto Sans";
  std::string watermark_font_path;
  std::string watermark_text_color = "#FFFFFF80";
  std::string watermark_outline_color = "#00000020";
  std::string watermark_back_color = "#00000000";
  int watermark_font_size = 28;
  int watermark_outline = 1;
  int watermark_shadow = 0;
  int watermark_margin = 24;
  bool watermark_bold = true;
  bool watermark_italic = false;
  bool watermark_bounce = false;
  float watermark_speed_x = 96.0f;
  float watermark_speed_y = 64.0f;
  float watermark_opacity = 0.18f;
  std::vector<Region> regions;

  static RenderJob fromPythonDict(const pybind11::dict& job_dict);
  void validate() const;
};

}  // namespace video_engine
