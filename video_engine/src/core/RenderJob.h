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
  int subtitle_font_scale = 4;
  int subtitle_margin = 8;
  float subtitle_opacity = 1.0f;
  std::vector<Region> regions;

  static RenderJob fromPythonDict(const pybind11::dict& job_dict);
  void validate() const;
};

}  // namespace video_engine
