#pragma once

#include <string>
#include <vector>

#include "core/SubtitleCue.h"

namespace video_engine {

class SrtParser {
public:
  static std::vector<SubtitleCue> parseFile(const std::string& path);
};

}  // namespace video_engine
