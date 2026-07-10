#pragma once

#include <string>
#include <vector>
#include <algorithm>

#include <pybind11/pybind11.h>

#include "core/Region.h"

namespace video_engine {

struct ImageOverlaySpec {
  std::string path;
  std::string width = "100%";
  std::string height = "100%";
  std::string resize_mode = "fit";
  float opacity = 1.0f;
  float position_x = 0.0f;
  float position_y = 0.0f;
};

struct RenderJob {
  std::string input;
  std::string output;
  int width = 0;
  int height = 0;
  double fps = 0.0;
  std::string video_aspect_ratio = "16:9";
  std::string bg_color = "#000000";
  float video_scale = 1.0f;
  bool flip_horizontal = false;
  std::string video_align_h = "center";
  std::string video_align_v = "center";
  std::string resize_mode = "fit";
  bool subtitle_gaussian_blur = true;
  std::string subtitle_srt;
  std::string subtitle_ass;
  std::string subtitle_renderer = "auto";
  std::string subtitle_text;
  std::string subtitle_font_family = "Noto Sans";
  std::string subtitle_font_path;
  std::string subtitle_text_color = "#FFF200";
  std::string subtitle_outline_color = "#101010";
  std::string subtitle_back_color = "#00000000";
  int subtitle_font_scale = 4;
  float subtitle_font_size = 1.5f;
  int subtitle_margin = 8;
  int subtitle_outline = 3;
  int subtitle_shadow = 0;
  bool subtitle_bold = true;
  bool subtitle_italic = true;
  bool subtitle_uppercase = false;
  float subtitle_opacity = 1.0f;
  bool subtitle_wrap = true;
  bool subtitle_clip = false;
  bool subtitle_auto_fit = true;
  int subtitle_padding_x = 0;
  int subtitle_padding_y = 0;
  std::string subtitle_align_h = "center";
  std::string subtitle_align_v = "middle";
  std::string logo_path;
  float logo_scale = 0.18f;
  float logo_opacity = 0.22f;
  int logo_margin = 24;
  bool logo_bounce = false;
  float logo_speed_x = 72.0f;
  float logo_speed_y = 48.0f;
  float logo_position_x = -1.0f;
  float logo_position_y = -1.0f;
  std::string watermark_text;
  std::string watermark_font_family = "Noto Sans";
  std::string watermark_font_path;
  std::string watermark_text_color = "#FFFFFF";
  std::string watermark_outline_color = "#000000";
  std::string watermark_back_color = "#00000000";
  float watermark_font_size = 4.0f;
  int watermark_outline = 1;
  int watermark_shadow = 0;
  int watermark_margin = 24;
  bool watermark_bold = true;
  bool watermark_italic = true;
  bool watermark_uppercase = false;
  bool watermark_bounce = false;
  float watermark_speed_x = 96.0f;
  float watermark_speed_y = 64.0f;
  float watermark_opacity = 0.28f;
  std::vector<Region> regions;
  std::vector<Region> subtitle_regions;
  std::vector<Region> blur_regions;
  std::vector<ImageOverlaySpec> image_overlays;

  static RenderJob fromPythonDict(const pybind11::dict& job_dict);
  void validate() const;
  [[nodiscard]] int resolveSubtitleFontPixels(int video_height) const {
    return std::clamp(static_cast<int>(subtitle_font_size * static_cast<float>(video_height) / 100.0f), 12, std::max(video_height / 3, 12));
  }
  [[nodiscard]] int resolveWatermarkFontPixels(int video_height) const {
    return std::clamp(static_cast<int>(watermark_font_size * static_cast<float>(video_height) / 100.0f), 10, std::max(video_height / 4, 10));
  }
};

}  // namespace video_engine
