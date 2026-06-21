#include "core/SubtitleRenderer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace video_engine {

namespace {

using Glyph = std::array<const char*, 7>;

const std::unordered_map<char, Glyph>& glyphs() {
  static const std::unordered_map<char, Glyph> kGlyphs = {
      {'A', {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}},
      {'B', {"11110", "10001", "10001", "11110", "10001", "10001", "11110"}},
      {'C', {"01111", "10000", "10000", "10000", "10000", "10000", "01111"}},
      {'D', {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}},
      {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
      {'F', {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}},
      {'G', {"01111", "10000", "10000", "10011", "10001", "10001", "01110"}},
      {'H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
      {'I', {"11111", "00100", "00100", "00100", "00100", "00100", "11111"}},
      {'J', {"00001", "00001", "00001", "00001", "10001", "10001", "01110"}},
      {'K', {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}},
      {'L', {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}},
      {'M', {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}},
      {'N', {"10001", "11001", "10101", "10011", "10001", "10001", "10001"}},
      {'O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
      {'P', {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}},
      {'Q', {"01110", "10001", "10001", "10001", "10101", "10010", "01101"}},
      {'R', {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}},
      {'S', {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}},
      {'T', {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}},
      {'U', {"10001", "10001", "10001", "10001", "10001", "10001", "01110"}},
      {'V', {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}},
      {'W', {"10001", "10001", "10001", "10101", "10101", "11011", "10001"}},
      {'X', {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}},
      {'Y', {"10001", "10001", "01010", "00100", "00100", "00100", "00100"}},
      {'Z', {"11111", "00001", "00010", "00100", "01000", "10000", "11111"}},
      {'0', {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}},
      {'1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
      {'2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
      {'3', {"11110", "00001", "00001", "01110", "00001", "00001", "11110"}},
      {'4', {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}},
      {'5', {"11111", "10000", "10000", "11110", "00001", "00001", "11110"}},
      {'6', {"01110", "10000", "10000", "11110", "10001", "10001", "01110"}},
      {'7', {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
      {'8', {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}},
      {'9', {"01110", "10001", "10001", "01111", "00001", "00001", "01110"}},
      {' ', {"00000", "00000", "00000", "00000", "00000", "00000", "00000"}},
      {'.', {"00000", "00000", "00000", "00000", "00000", "00110", "00110"}},
      {',', {"00000", "00000", "00000", "00000", "00110", "00110", "00100"}},
      {'!', {"00100", "00100", "00100", "00100", "00100", "00000", "00100"}},
      {'?', {"01110", "10001", "00001", "00010", "00100", "00000", "00100"}},
      {':', {"00000", "00100", "00100", "00000", "00100", "00100", "00000"}},
      {';', {"00000", "00100", "00100", "00000", "00100", "00100", "01000"}},
      {'-', {"00000", "00000", "00000", "11111", "00000", "00000", "00000"}},
      {'(', {"00010", "00100", "01000", "01000", "01000", "00100", "00010"}},
      {')', {"01000", "00100", "00010", "00010", "00010", "00100", "01000"}},
      {'/', {"00001", "00010", "00100", "01000", "10000", "00000", "00000"}},
      {'\'', {"00100", "00100", "00000", "00000", "00000", "00000", "00000"}},
      {'"', {"01010", "01010", "00000", "00000", "00000", "00000", "00000"}},
      {'&', {"01100", "10010", "10100", "01000", "10101", "10010", "01101"}},
  };
  return kGlyphs;
}

Glyph glyphFor(char value) {
  const auto upper = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
  const auto found = glyphs().find(upper);
  if (found != glyphs().end()) {
    return found->second;
  }
  return glyphs().at('?');
}

std::string normalizeText(const std::string& text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (char ch : text) {
    if (ch == '\r') {
      continue;
    }
    normalized.push_back(ch);
  }
  return normalized;
}

std::vector<std::string> wrapLines(const std::string& text, int max_chars_per_line) {
  std::vector<std::string> output;
  std::istringstream lines(normalizeText(text));
  std::string raw_line;
  while (std::getline(lines, raw_line, '\n')) {
    std::istringstream words(raw_line);
    std::string word;
    std::string current;
    while (words >> word) {
      const std::string candidate = current.empty() ? word : current + " " + word;
      if (static_cast<int>(candidate.size()) > max_chars_per_line && !current.empty()) {
        output.push_back(current);
        current = word;
      } else {
        current = candidate;
      }
    }
    if (!current.empty()) {
      output.push_back(current);
    }
    if (raw_line.empty()) {
      output.emplace_back();
    }
  }
  if (output.empty()) {
    output.push_back(text);
  }
  return output;
}

}  // namespace

SubtitleOverlay SubtitleRenderer::buildOverlay(
    const Region& region,
    const std::string& text,
    int video_width,
    int video_height,
    int font_scale,
    int margin,
    float opacity) {
  SubtitleOverlay overlay;
  if (text.empty() || region.w <= 0 || region.h <= 0) {
    return overlay;
  }

  const int glyph_width = 5 * std::max(font_scale, 1);
  const int glyph_height = 7 * std::max(font_scale, 1);
  const int glyph_spacing = std::max(font_scale, 1);
  const int line_spacing = std::max(font_scale, 1) * 2;
  const int usable_width = std::max(region.w - margin * 2, glyph_width);
  const int max_chars_per_line = std::max(1, usable_width / (glyph_width + glyph_spacing));
  const std::vector<std::string> lines = wrapLines(text, max_chars_per_line);

  const int text_width = [&]() {
    int max_width = 0;
    for (const std::string& line : lines) {
      const int width = std::max(0, static_cast<int>(line.size()) * (glyph_width + glyph_spacing) - glyph_spacing);
      max_width = std::max(max_width, width);
    }
    return max_width;
  }();
  const int text_height =
      std::max(0, static_cast<int>(lines.size()) * (glyph_height + line_spacing) - line_spacing);

  overlay.width = std::min(region.w, text_width);
  overlay.height = std::min(region.h, text_height);
  overlay.stride = overlay.width;
  overlay.x = std::clamp(region.x + (region.w - overlay.width) / 2, 0, std::max(video_width - overlay.width, 0));
  overlay.y = std::clamp(region.y + (region.h - overlay.height) / 2, 0, std::max(video_height - overlay.height, 0));
  overlay.opacity = std::clamp(opacity, 0.0f, 1.0f);
  overlay.enabled = overlay.width > 0 && overlay.height > 0;
  overlay.cue_text = text;
  overlay.alpha_mask.assign(static_cast<size_t>(overlay.width) * static_cast<size_t>(overlay.height), 0);

  for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
    const std::string& line = lines[line_index];
    const int line_width =
        std::max(0, static_cast<int>(line.size()) * (glyph_width + glyph_spacing) - glyph_spacing);
    int cursor_x = std::max(0, (overlay.width - line_width) / 2);
    const int cursor_y = static_cast<int>(line_index) * (glyph_height + line_spacing);

    for (char ch : line) {
      const Glyph glyph = glyphFor(ch);
      for (int gy = 0; gy < 7; ++gy) {
        for (int gx = 0; gx < 5; ++gx) {
          if (glyph[gy][gx] != '1') {
            continue;
          }
          for (int sy = 0; sy < font_scale; ++sy) {
            for (int sx = 0; sx < font_scale; ++sx) {
              const int x = cursor_x + gx * font_scale + sx;
              const int y = cursor_y + gy * font_scale + sy;
              if (x >= 0 && x < overlay.width && y >= 0 && y < overlay.height) {
                overlay.alpha_mask[static_cast<size_t>(y) * static_cast<size_t>(overlay.stride) +
                                   static_cast<size_t>(x)] = 255;
              }
            }
          }
        }
      }
      cursor_x += glyph_width + glyph_spacing;
    }
  }

  return overlay;
}

}  // namespace video_engine
