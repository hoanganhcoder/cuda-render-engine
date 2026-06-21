#include "core/SrtParser.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace video_engine {

namespace {

std::string trim(const std::string& value) {
  const size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

double parseTimestamp(const std::string& text) {
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  int millis = 0;
  if (sscanf(text.c_str(), "%d:%d:%d,%d", &hours, &minutes, &seconds, &millis) != 4) {
    throw std::runtime_error("Invalid SRT timestamp: " + text);
  }
  return static_cast<double>(hours) * 3600.0 + static_cast<double>(minutes) * 60.0 +
         static_cast<double>(seconds) + static_cast<double>(millis) / 1000.0;
}

std::string stripInlineTags(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  bool in_tag = false;
  for (char ch : text) {
    if (ch == '<') {
      in_tag = true;
      continue;
    }
    if (ch == '>') {
      in_tag = false;
      continue;
    }
    if (!in_tag) {
      result.push_back(ch);
    }
  }
  return trim(result);
}

}  // namespace

std::vector<SubtitleCue> SrtParser::parseFile(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to open subtitle SRT file: " + path);
  }

  std::stringstream buffer;
  buffer << input.rdbuf();
  std::string content = buffer.str();
  if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
      static_cast<unsigned char>(content[1]) == 0xBB && static_cast<unsigned char>(content[2]) == 0xBF) {
    content = content.substr(3);
  }

  std::vector<SubtitleCue> cues;
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    line = trim(line);
    if (line.empty()) {
      continue;
    }

    std::string time_line;
    if (line.find("-->") != std::string::npos) {
      time_line = line;
    } else {
      if (!std::getline(stream, time_line)) {
        break;
      }
      time_line = trim(time_line);
    }

    const size_t arrow = time_line.find("-->");
    if (arrow == std::string::npos) {
      continue;
    }

    SubtitleCue cue;
    cue.start = parseTimestamp(trim(time_line.substr(0, arrow)));
    cue.end = parseTimestamp(trim(time_line.substr(arrow + 3)));

    std::string text_line;
    std::string combined_text;
    while (std::getline(stream, text_line)) {
      text_line = trim(text_line);
      if (text_line.empty()) {
        break;
      }
      if (!combined_text.empty()) {
        combined_text.push_back('\n');
      }
      combined_text += stripInlineTags(text_line);
    }

    cue.text = trim(combined_text);
    if (!cue.text.empty()) {
      cues.push_back(std::move(cue));
    }
  }

  return cues;
}

}  // namespace video_engine
