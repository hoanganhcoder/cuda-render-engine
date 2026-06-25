#include "core/TextBoxRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/SrtParser.h"

#if defined(VIDEO_ENGINE_HAS_TEXTBOX_RENDERER)
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_STROKER_H
#include FT_SYNTHESIS_H
#include <hb-ft.h>
#include <hb.h>
#endif

namespace video_engine {

namespace {

struct RgbaColor {
  uint8_t red = 255;
  uint8_t green = 255;
  uint8_t blue = 255;
  float alpha = 1.0f;
};

void rgbToYuv(uint8_t red, uint8_t green, uint8_t blue, uint8_t& y, uint8_t& u, uint8_t& v) {
  const float yf = 0.299f * red + 0.587f * green + 0.114f * blue;
  const float uf = -0.168736f * red - 0.331264f * green + 0.5f * blue + 128.0f;
  const float vf = 0.5f * red - 0.418688f * green - 0.081312f * blue + 128.0f;
  y = static_cast<uint8_t>(std::clamp(yf, 0.0f, 255.0f));
  u = static_cast<uint8_t>(std::clamp(uf, 0.0f, 255.0f));
  v = static_cast<uint8_t>(std::clamp(vf, 0.0f, 255.0f));
}

char32_t decodeUtf8CodePoint(const std::string& text, size_t& index) {
  if (index >= text.size()) {
    return 0;
  }

  const unsigned char lead = static_cast<unsigned char>(text[index]);
  if (lead < 0x80) {
    ++index;
    return static_cast<char32_t>(lead);
  }

  size_t width = 0;
  char32_t code_point = 0;
  if ((lead & 0xE0) == 0xC0) {
    width = 2;
    code_point = static_cast<char32_t>(lead & 0x1F);
  } else if ((lead & 0xF0) == 0xE0) {
    width = 3;
    code_point = static_cast<char32_t>(lead & 0x0F);
  } else if ((lead & 0xF8) == 0xF0) {
    width = 4;
    code_point = static_cast<char32_t>(lead & 0x07);
  } else {
    ++index;
    return static_cast<char32_t>(lead);
  }

  if (index + width > text.size()) {
    ++index;
    return static_cast<char32_t>(lead);
  }

  for (size_t offset = 1; offset < width; ++offset) {
    const unsigned char byte = static_cast<unsigned char>(text[index + offset]);
    if ((byte & 0xC0) != 0x80) {
      ++index;
      return static_cast<char32_t>(lead);
    }
    code_point = (code_point << 6) | static_cast<char32_t>(byte & 0x3F);
  }

  index += width;
  return code_point;
}

void appendUtf8CodePoint(std::string& output, char32_t code_point) {
  if (code_point <= 0x7F) {
    output.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | ((code_point >> 6) & 0x1F)));
    output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else if (code_point <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | ((code_point >> 12) & 0x0F)));
    output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xF0 | ((code_point >> 18) & 0x07)));
    output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  }
}

char32_t uppercaseCodePoint(char32_t code_point) {
  if (code_point >= U'a' && code_point <= U'z') {
    return code_point - (U'a' - U'A');
  }

  switch (code_point) {
    case U'\u00E0': return U'\u00C0';
    case U'\u00E1': return U'\u00C1';
    case U'\u1EA3': return U'\u1EA2';
    case U'\u00E3': return U'\u00C3';
    case U'\u1EA1': return U'\u1EA0';
    case U'\u0103': return U'\u0102';
    case U'\u1EB1': return U'\u1EB0';
    case U'\u1EAF': return U'\u1EAE';
    case U'\u1EB3': return U'\u1EB2';
    case U'\u1EB5': return U'\u1EB4';
    case U'\u1EB7': return U'\u1EB6';
    case U'\u00E2': return U'\u00C2';
    case U'\u1EA7': return U'\u1EA6';
    case U'\u1EA5': return U'\u1EA4';
    case U'\u1EA9': return U'\u1EA8';
    case U'\u1EAB': return U'\u1EAA';
    case U'\u1EAD': return U'\u1EAC';
    case U'\u00E8': return U'\u00C8';
    case U'\u00E9': return U'\u00C9';
    case U'\u1EBB': return U'\u1EBA';
    case U'\u1EBD': return U'\u1EBC';
    case U'\u1EB9': return U'\u1EB8';
    case U'\u00EA': return U'\u00CA';
    case U'\u1EC1': return U'\u1EC0';
    case U'\u1EBF': return U'\u1EBE';
    case U'\u1EC3': return U'\u1EC2';
    case U'\u1EC5': return U'\u1EC4';
    case U'\u1EC7': return U'\u1EC6';
    case U'\u00EC': return U'\u00CC';
    case U'\u00ED': return U'\u00CD';
    case U'\u1EC9': return U'\u1EC8';
    case U'\u0129': return U'\u0128';
    case U'\u1ECB': return U'\u1ECA';
    case U'\u00F2': return U'\u00D2';
    case U'\u00F3': return U'\u00D3';
    case U'\u1ECF': return U'\u1ECE';
    case U'\u00F5': return U'\u00D5';
    case U'\u1ECD': return U'\u1ECC';
    case U'\u00F4': return U'\u00D4';
    case U'\u1ED3': return U'\u1ED2';
    case U'\u1ED1': return U'\u1ED0';
    case U'\u1ED5': return U'\u1ED4';
    case U'\u1ED7': return U'\u1ED6';
    case U'\u1ED9': return U'\u1ED8';
    case U'\u01A1': return U'\u01A0';
    case U'\u1EDD': return U'\u1EDC';
    case U'\u1EDB': return U'\u1EDA';
    case U'\u1EDF': return U'\u1EDE';
    case U'\u1EE1': return U'\u1EE0';
    case U'\u1EE3': return U'\u1EE2';
    case U'\u00F9': return U'\u00D9';
    case U'\u00FA': return U'\u00DA';
    case U'\u1EE7': return U'\u1EE6';
    case U'\u0169': return U'\u0168';
    case U'\u1EE5': return U'\u1EE4';
    case U'\u01B0': return U'\u01AF';
    case U'\u1EEB': return U'\u1EEA';
    case U'\u1EE9': return U'\u1EE8';
    case U'\u1EED': return U'\u1EEC';
    case U'\u1EEF': return U'\u1EEE';
    case U'\u1EF1': return U'\u1EF0';
    case U'\u1EF3': return U'\u1EF2';
    case U'\u00FD': return U'\u00DD';
    case U'\u1EF7': return U'\u1EF6';
    case U'\u1EF9': return U'\u1EF8';
    case U'\u1EF5': return U'\u1EF4';
    case U'\u0111': return U'\u0110';
    default: return code_point;
  }
}

std::string uppercaseUnicode(const std::string& text) {
  std::string output;
  output.reserve(text.size());
  for (size_t index = 0; index < text.size();) {
    appendUtf8CodePoint(output, uppercaseCodePoint(decodeUtf8CodePoint(text, index)));
  }
  return output;
}

std::vector<std::string> utf8CodePointChunks(const std::string& text) {
  std::vector<std::string> chunks;
  chunks.reserve(text.size());
  for (size_t index = 0; index < text.size();) {
    const size_t start = index;
    decodeUtf8CodePoint(text, index);
    chunks.push_back(text.substr(start, index - start));
  }
  return chunks;
}

RgbaColor parseHexColor(const std::string& hex) {
  auto parse_component = [&](size_t offset) {
    return static_cast<unsigned int>(std::stoul(hex.substr(offset, 2), nullptr, 16));
  };

  RgbaColor color{};
  color.red = static_cast<uint8_t>(parse_component(1));
  color.green = static_cast<uint8_t>(parse_component(3));
  color.blue = static_cast<uint8_t>(parse_component(5));
  if (hex.size() == 9) {
    color.alpha = static_cast<float>(parse_component(7)) / 255.0f;
  }
  return color;
}

float clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

std::string normalizeCueText(const std::string& text, bool uppercase) {
  if (!uppercase) {
    return text;
  }
  return uppercaseUnicode(text);
}

#if defined(VIDEO_ENGINE_HAS_TEXTBOX_RENDERER)
struct GlyphCacheKey {
  uint32_t glyph_index = 0;
  int font_pixels = 0;
  int outline = 0;
  bool bold = false;
  bool italic = false;

  bool operator==(const GlyphCacheKey& other) const {
    return glyph_index == other.glyph_index && font_pixels == other.font_pixels && outline == other.outline &&
           bold == other.bold && italic == other.italic;
  }
};

struct GlyphCacheValue {
  int fill_left = 0;
  int fill_top = 0;
  int fill_width = 0;
  int fill_height = 0;
  int outline_left = 0;
  int outline_top = 0;
  int outline_width = 0;
  int outline_height = 0;
  std::vector<uint8_t> fill_alpha;
  std::vector<uint8_t> outline_alpha;
};

struct ShapedGlyph {
  uint32_t glyph_index = 0;
  float x_offset = 0.0f;
  float y_offset = 0.0f;
  float x_advance = 0.0f;
};

struct LineLayout {
  std::string text;
  std::vector<ShapedGlyph> glyphs;
  float width = 0.0f;
  float advance_width = 0.0f;
};

struct OverlayCacheKey {
  size_t cue_index = 0;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  bool operator==(const OverlayCacheKey& other) const {
    return cue_index == other.cue_index && x == other.x && y == other.y && w == other.w && h == other.h;
  }
};

struct GlyphCacheKeyHasher {
  size_t operator()(const GlyphCacheKey& key) const {
    size_t hash = static_cast<size_t>(key.glyph_index);
    hash = hash * 1315423911u + static_cast<size_t>(key.font_pixels);
    hash = hash * 1315423911u + static_cast<size_t>(key.outline);
    hash = hash * 1315423911u + static_cast<size_t>(key.bold);
    hash = hash * 1315423911u + static_cast<size_t>(key.italic);
    return hash;
  }
};

struct OverlayCacheKeyHasher {
  size_t operator()(const OverlayCacheKey& key) const {
    size_t hash = key.cue_index;
    hash = hash * 1315423911u + static_cast<size_t>(key.x);
    hash = hash * 1315423911u + static_cast<size_t>(key.y);
    hash = hash * 1315423911u + static_cast<size_t>(key.w);
    hash = hash * 1315423911u + static_cast<size_t>(key.h);
    return hash;
  }
};

int computeHorizontalSafetyPadding(int margin, int font_pixels, int outline, bool italic) {
  const int outline_padding = std::max(outline * 2, 4);
  const int italic_padding = italic ? std::max(font_pixels / 8, 4) : 2;
  return std::max(margin, outline_padding + italic_padding);
}

void blendOverlayPixel(SubtitleOverlay& overlay, int x, int y, float alpha, const RgbaColor& color) {
  if (alpha <= 0.0f || x < 0 || y < 0 || x >= overlay.width || y >= overlay.height) {
    return;
  }

  const size_t index = static_cast<size_t>(y) * static_cast<size_t>(overlay.stride) + static_cast<size_t>(x);
  const float src_alpha = clamp01(alpha * color.alpha);
  const float dst_alpha = static_cast<float>(overlay.alpha_mask[index]) / 255.0f;
  const float out_alpha = src_alpha + dst_alpha * (1.0f - src_alpha);
  if (out_alpha <= 0.0f) {
    return;
  }

  uint8_t yuv_y = 0;
  uint8_t yuv_u = 128;
  uint8_t yuv_v = 128;
  rgbToYuv(color.red, color.green, color.blue, yuv_y, yuv_u, yuv_v);

  const float dst_weight = dst_alpha * (1.0f - src_alpha);
  const float src_weight = src_alpha;
  overlay.alpha_mask[index] = static_cast<uint8_t>(std::lround(out_alpha * 255.0f));
  overlay.luma_mask[index] = static_cast<uint8_t>(std::lround(
      (src_weight * static_cast<float>(yuv_y) + dst_weight * static_cast<float>(overlay.luma_mask[index])) / out_alpha));
  overlay.chroma_u_mask[index] = static_cast<uint8_t>(std::lround(
      (src_weight * static_cast<float>(yuv_u) + dst_weight * static_cast<float>(overlay.chroma_u_mask[index])) / out_alpha));
  overlay.chroma_v_mask[index] = static_cast<uint8_t>(std::lround(
      (src_weight * static_cast<float>(yuv_v) + dst_weight * static_cast<float>(overlay.chroma_v_mask[index])) / out_alpha));
}

std::vector<uint8_t> flattenFtBitmap(const FT_Bitmap& bitmap) {
  std::vector<uint8_t> pixels(static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.rows), 0);
  for (unsigned int row = 0; row < bitmap.rows; ++row) {
    const uint8_t* src = bitmap.buffer + static_cast<size_t>(row) * static_cast<size_t>(bitmap.pitch);
    uint8_t* dst = pixels.data() + static_cast<size_t>(row) * static_cast<size_t>(bitmap.width);
    std::memcpy(dst, src, bitmap.width);
  }
  return pixels;
}
#endif

}  // namespace

struct TextBoxRenderer::Impl {
  RenderJob job;
  int video_width = 0;
  int video_height = 0;
  int font_pixels = 0;
  std::vector<SubtitleCue> cues;
#if defined(VIDEO_ENGINE_HAS_TEXTBOX_RENDERER)
  FT_Library ft_library = nullptr;
  FT_Face ft_face = nullptr;
  hb_font_t* hb_font = nullptr;
  bool apply_synthetic_bold = false;
  bool apply_synthetic_italic = false;
  mutable std::unordered_map<GlyphCacheKey, GlyphCacheValue, GlyphCacheKeyHasher> glyph_cache;
  mutable std::unordered_map<std::string, LineLayout> line_cache;
  mutable std::unordered_map<OverlayCacheKey, SubtitleOverlay, OverlayCacheKeyHasher> overlay_cache;
#endif
};

TextBoxRenderer::TextBoxRenderer() : impl_(std::make_unique<Impl>()) {
}

TextBoxRenderer::~TextBoxRenderer() {
#if defined(VIDEO_ENGINE_HAS_TEXTBOX_RENDERER)
  if (impl_->hb_font != nullptr) {
    hb_font_destroy(impl_->hb_font);
    impl_->hb_font = nullptr;
  }
  if (impl_->ft_face != nullptr) {
    FT_Done_Face(impl_->ft_face);
    impl_->ft_face = nullptr;
  }
  if (impl_->ft_library != nullptr) {
    FT_Done_FreeType(impl_->ft_library);
    impl_->ft_library = nullptr;
  }
#endif
}

void TextBoxRenderer::initialize(const RenderJob& job, int video_width, int video_height) {
#if defined(VIDEO_ENGINE_HAS_TEXTBOX_RENDERER)
  impl_->glyph_cache.clear();
  impl_->line_cache.clear();
  impl_->overlay_cache.clear();
  if (impl_->hb_font != nullptr) {
    hb_font_destroy(impl_->hb_font);
    impl_->hb_font = nullptr;
  }
  if (impl_->ft_face != nullptr) {
    FT_Done_Face(impl_->ft_face);
    impl_->ft_face = nullptr;
  }
  if (impl_->ft_library != nullptr) {
    FT_Done_FreeType(impl_->ft_library);
    impl_->ft_library = nullptr;
  }
#endif
  impl_->job = job;
  impl_->video_width = video_width;
  impl_->video_height = video_height;
  impl_->font_pixels = job.resolveSubtitleFontPixels(video_height);
  impl_->cues.clear();
  available_ = false;

  if (!job.subtitle_srt.empty()) {
    impl_->cues = SrtParser::parseFile(job.subtitle_srt);
  } else if (!job.subtitle_text.empty()) {
    impl_->cues.push_back(SubtitleCue{0.0, 1.0e12, job.subtitle_text});
  }

#if !defined(VIDEO_ENGINE_HAS_TEXTBOX_RENDERER)
  return;
#else
  if (impl_->cues.empty() || job.subtitle_font_path.empty()) {
    return;
  }

  if (FT_Init_FreeType(&impl_->ft_library) != 0) {
    return;
  }
  if (FT_New_Face(impl_->ft_library, job.subtitle_font_path.c_str(), 0, &impl_->ft_face) != 0) {
    return;
  }
  const bool face_is_bold = (impl_->ft_face->style_flags & FT_STYLE_FLAG_BOLD) != 0;
  const bool face_is_italic = (impl_->ft_face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
  const bool explicit_face_file = !job.subtitle_font_path.empty();
  impl_->apply_synthetic_bold = job.subtitle_bold && !face_is_bold && !explicit_face_file;
  impl_->apply_synthetic_italic = job.subtitle_italic && !face_is_italic && !explicit_face_file;
  FT_Set_Pixel_Sizes(impl_->ft_face, 0, static_cast<FT_UInt>(impl_->font_pixels));
  impl_->hb_font = hb_ft_font_create_referenced(impl_->ft_face);
  if (impl_->hb_font == nullptr) {
    return;
  }

  available_ = true;
#endif
}

SubtitleOverlay TextBoxRenderer::render(double timestamp_seconds, const Region* anchor_region) const {
  SubtitleOverlay overlay;
#if !defined(VIDEO_ENGINE_HAS_TEXTBOX_RENDERER)
  (void)timestamp_seconds;
  (void)anchor_region;
  return overlay;
#else
  if (!available_ || anchor_region == nullptr || anchor_region->w <= 0 || anchor_region->h <= 0) {
    return overlay;
  }

  size_t cue_index = impl_->cues.size();
  for (size_t index = 0; index < impl_->cues.size(); ++index) {
    if (impl_->cues[index].isActive(timestamp_seconds)) {
      cue_index = index;
      break;
    }
  }
  if (cue_index == impl_->cues.size()) {
    return overlay;
  }

  const OverlayCacheKey cache_key{cue_index, anchor_region->x, anchor_region->y, anchor_region->w, anchor_region->h};
  const auto cached = impl_->overlay_cache.find(cache_key);
  if (cached != impl_->overlay_cache.end()) {
    return cached->second;
  }

  const SubtitleCue& cue = impl_->cues[cue_index];
  const std::string normalized_text = normalizeCueText(cue.text, impl_->job.subtitle_uppercase);
  auto set_font_size = [&](int font_pixels) {
    FT_Set_Pixel_Sizes(impl_->ft_face, 0, static_cast<FT_UInt>(font_pixels));
    hb_ft_font_changed(impl_->hb_font);
  };

  struct FittedLayout {
    int font_pixels = 0;
    int usable_width = 0;
    int usable_height = 0;
    int line_height = 0;
    int side_padding = 0;
    int top_padding = 0;
    float max_line_width = 0.0f;
    std::vector<std::string> lines;
  };

  auto shape_lines_for_font = [&](int font_pixels) -> FittedLayout {
    set_font_size(font_pixels);
    const int intrinsic_side_padding = computeHorizontalSafetyPadding(
        std::max(impl_->job.subtitle_margin / 2, 0),
        font_pixels,
        impl_->job.subtitle_outline,
        impl_->job.subtitle_italic);
    const int dynamic_side_padding = std::max(impl_->job.subtitle_padding_x, intrinsic_side_padding);
    const int dynamic_top_padding = std::max(
        impl_->job.subtitle_padding_y,
        std::max(impl_->job.subtitle_margin / 2, impl_->job.subtitle_outline * 2 + impl_->job.subtitle_shadow));
    const int dynamic_usable_width = std::max(1, anchor_region->w - dynamic_side_padding * 2);

    auto get_line_layout = [&](const std::string& line_text) -> const LineLayout& {
      const std::string cache_id =
          line_text + "|" + std::to_string(dynamic_usable_width) + "|" + std::to_string(font_pixels) + "|" +
          std::to_string(impl_->job.subtitle_outline) + "|" + std::to_string(impl_->apply_synthetic_bold) + "|" +
          std::to_string(impl_->apply_synthetic_italic);
      auto found = impl_->line_cache.find(cache_id);
      if (found != impl_->line_cache.end()) {
        return found->second;
      }

      hb_buffer_t* buffer = hb_buffer_create();
      hb_buffer_add_utf8(buffer, line_text.c_str(), static_cast<int>(line_text.size()), 0, static_cast<int>(line_text.size()));
      hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
      hb_buffer_set_language(buffer, hb_language_from_string("vi", -1));
      hb_buffer_guess_segment_properties(buffer);
      hb_shape(impl_->hb_font, buffer, nullptr, 0);

      unsigned int glyph_count = 0;
      const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
      const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);

      LineLayout layout;
      layout.text = line_text;
      float pen_x = 0.0f;
      float min_x = std::numeric_limits<float>::max();
      float max_x = std::numeric_limits<float>::lowest();
      for (unsigned int glyph_index = 0; glyph_index < glyph_count; ++glyph_index) {
        ShapedGlyph glyph{};
        glyph.glyph_index = infos[glyph_index].codepoint;
        glyph.x_offset = static_cast<float>(positions[glyph_index].x_offset) / 64.0f;
        glyph.y_offset = static_cast<float>(positions[glyph_index].y_offset) / 64.0f;
        glyph.x_advance = static_cast<float>(positions[glyph_index].x_advance) / 64.0f;
        layout.glyphs.push_back(glyph);
        if (FT_Load_Glyph(impl_->ft_face, glyph.glyph_index, FT_LOAD_DEFAULT) == 0) {
          const float glyph_left = pen_x + glyph.x_offset + static_cast<float>(impl_->ft_face->glyph->metrics.horiBearingX >> 6);
          const float glyph_right = glyph_left + static_cast<float>(impl_->ft_face->glyph->metrics.width >> 6);
          min_x = std::min(min_x, glyph_left);
          max_x = std::max(max_x, glyph_right);
        }
        pen_x += glyph.x_advance;
      }
      layout.advance_width = pen_x;
      layout.width = (min_x <= max_x) ? (max_x - min_x) : pen_x;
      hb_buffer_destroy(buffer);
      return impl_->line_cache.emplace(cache_id, std::move(layout)).first->second;
    };

    auto split_word_to_fit = [&](const std::string& word) {
      std::vector<std::string> segments;
      std::string current_segment;
      for (const std::string& code_point : utf8CodePointChunks(word)) {
        const std::string candidate = current_segment + code_point;
        const LineLayout& candidate_layout = get_line_layout(candidate);
        if (!current_segment.empty() && candidate_layout.advance_width > static_cast<float>(dynamic_usable_width)) {
          segments.push_back(current_segment);
          current_segment = code_point;
        } else {
          current_segment = candidate;
        }
      }
      if (!current_segment.empty()) {
        segments.push_back(current_segment);
      }
      if (segments.empty()) {
        segments.push_back(word);
      }
      return segments;
    };

    std::vector<std::string> lines;
    {
      std::istringstream input(normalized_text);
      std::string raw_line;
      while (std::getline(input, raw_line, '\n')) {
        if (!impl_->job.subtitle_wrap) {
          lines.push_back(raw_line);
          continue;
        }
        std::istringstream words(raw_line);
        std::string word;
        std::string current;
        while (words >> word) {
          if (current.empty() && get_line_layout(word).advance_width > static_cast<float>(dynamic_usable_width)) {
            const std::vector<std::string> segments = split_word_to_fit(word);
            for (size_t segment_index = 0; segment_index < segments.size(); ++segment_index) {
              const std::string& segment = segments[segment_index];
              if (segment_index + 1 < segments.size()) {
                lines.push_back(segment);
              } else {
                current = segment;
              }
            }
            continue;
          }
          const std::string candidate = current.empty() ? word : current + " " + word;
          const LineLayout& candidate_layout = get_line_layout(candidate);
          if (!current.empty() && candidate_layout.advance_width > static_cast<float>(dynamic_usable_width)) {
            lines.push_back(current);
            if (get_line_layout(word).advance_width > static_cast<float>(dynamic_usable_width)) {
              const std::vector<std::string> segments = split_word_to_fit(word);
              for (size_t segment_index = 0; segment_index < segments.size(); ++segment_index) {
                const std::string& segment = segments[segment_index];
                if (segment_index + 1 < segments.size()) {
                  lines.push_back(segment);
                } else {
                  current = segment;
                }
              }
            } else {
              current = word;
            }
          } else {
            current = candidate;
          }
        }
        if (!current.empty()) {
          lines.push_back(current);
        } else if (raw_line.empty()) {
          lines.emplace_back();
        }
      }
    }
    if (lines.empty()) {
      lines.push_back(normalized_text);
    }

    const int ascender = std::max<int>(1, static_cast<int>(impl_->ft_face->size->metrics.ascender >> 6));
    const int line_height = std::max<int>(
        ascender + static_cast<int>(impl_->job.subtitle_outline) * 2 + impl_->job.subtitle_shadow + 4,
        static_cast<int>(impl_->ft_face->size->metrics.height >> 6));
    const int dynamic_usable_height = std::max(1, anchor_region->h - dynamic_top_padding * 2);
    float max_line_width = 0.0f;
    for (const std::string& line : lines) {
      max_line_width = std::max(max_line_width, get_line_layout(line).advance_width);
    }
    return FittedLayout{
        font_pixels,
        dynamic_usable_width,
        dynamic_usable_height,
        line_height,
        dynamic_side_padding,
        dynamic_top_padding,
        max_line_width,
        std::move(lines)};
  };

  const int requested_font_pixels = impl_->font_pixels;
  const int min_font_pixels = std::max(12, static_cast<int>(std::floor(static_cast<float>(requested_font_pixels) * 0.55f)));
  FittedLayout fitted = shape_lines_for_font(requested_font_pixels);
  if (impl_->job.subtitle_auto_fit) {
    for (int font_pixels = requested_font_pixels; font_pixels >= min_font_pixels; --font_pixels) {
      FittedLayout candidate = shape_lines_for_font(font_pixels);
      const int total_height_candidate = candidate.line_height * static_cast<int>(candidate.lines.size());
      if (total_height_candidate <= candidate.usable_height &&
          candidate.max_line_width <= static_cast<float>(candidate.usable_width)) {
        fitted = std::move(candidate);
        break;
      }
      fitted = std::move(candidate);
    }
  }

  set_font_size(fitted.font_pixels);
  const int ascender = std::max<int>(1, static_cast<int>(impl_->ft_face->size->metrics.ascender >> 6));
  const auto get_line_layout = [&](const std::string& line_text) -> const LineLayout& {
    const std::string cache_id =
        line_text + "|" + std::to_string(fitted.usable_width) + "|" + std::to_string(fitted.font_pixels) + "|" +
        std::to_string(impl_->job.subtitle_outline) + "|" + std::to_string(impl_->apply_synthetic_bold) + "|" +
        std::to_string(impl_->apply_synthetic_italic);
    return impl_->line_cache.at(cache_id);
  };

  const std::vector<std::string>& lines = fitted.lines;
  const int line_height = fitted.line_height;
  const int side_padding = fitted.side_padding;
  const int top_padding = fitted.top_padding;
  const int total_height = line_height * static_cast<int>(lines.size());
  const int usable_height = fitted.usable_height;
  std::string align_h = impl_->job.subtitle_align_h;
  std::transform(align_h.begin(), align_h.end(), align_h.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  std::string align_v = impl_->job.subtitle_align_v;
  std::transform(align_v.begin(), align_v.end(), align_v.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  int base_top = anchor_region->y + top_padding;
  if (align_v == "middle") {
    base_top += std::max(usable_height - total_height, 0) / 2;
  } else if (align_v == "bottom") {
    base_top += std::max(usable_height - total_height, 0);
  }

  struct DrawGlyph {
    const GlyphCacheValue* bitmap = nullptr;
    int fill_x = 0;
    int fill_y = 0;
    int outline_x = 0;
    int outline_y = 0;
  };
  std::vector<DrawGlyph> draw_glyphs;
  draw_glyphs.reserve(lines.size() * 32);
  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int max_y = std::numeric_limits<int>::min();

  auto build_glyph = [&](uint32_t glyph_index) -> const GlyphCacheValue& {
    const GlyphCacheKey key{
        glyph_index,
        fitted.font_pixels,
        impl_->job.subtitle_outline,
        impl_->apply_synthetic_bold,
        impl_->apply_synthetic_italic};
    auto found = impl_->glyph_cache.find(key);
    if (found != impl_->glyph_cache.end()) {
      return found->second;
    }

    FT_Load_Glyph(impl_->ft_face, glyph_index, FT_LOAD_DEFAULT);
    if (impl_->apply_synthetic_bold) {
      FT_GlyphSlot_Embolden(impl_->ft_face->glyph);
    }
    if (impl_->apply_synthetic_italic) {
      FT_GlyphSlot_Oblique(impl_->ft_face->glyph);
    }

    FT_Glyph fill_glyph = nullptr;
    FT_Get_Glyph(impl_->ft_face->glyph, &fill_glyph);
    FT_Glyph_To_Bitmap(&fill_glyph, FT_RENDER_MODE_NORMAL, nullptr, 1);
    const FT_BitmapGlyph fill_bitmap = reinterpret_cast<FT_BitmapGlyph>(fill_glyph);

    GlyphCacheValue value{};
    value.fill_left = fill_bitmap->left;
    value.fill_top = fill_bitmap->top;
    value.fill_width = static_cast<int>(fill_bitmap->bitmap.width);
    value.fill_height = static_cast<int>(fill_bitmap->bitmap.rows);
    value.fill_alpha = flattenFtBitmap(fill_bitmap->bitmap);

    if (impl_->job.subtitle_outline > 0) {
      FT_Load_Glyph(impl_->ft_face, glyph_index, FT_LOAD_DEFAULT);
      if (impl_->apply_synthetic_bold) {
        FT_GlyphSlot_Embolden(impl_->ft_face->glyph);
      }
      if (impl_->apply_synthetic_italic) {
        FT_GlyphSlot_Oblique(impl_->ft_face->glyph);
      }
      FT_Glyph outline_glyph = nullptr;
      FT_Get_Glyph(impl_->ft_face->glyph, &outline_glyph);
      FT_Stroker stroker = nullptr;
      FT_Stroker_New(impl_->ft_library, &stroker);
      FT_Stroker_Set(
          stroker,
          static_cast<FT_Fixed>(impl_->job.subtitle_outline * 64),
          FT_STROKER_LINECAP_ROUND,
          FT_STROKER_LINEJOIN_ROUND,
          0);
      FT_Glyph_StrokeBorder(&outline_glyph, stroker, 0, 1);
      FT_Glyph_To_Bitmap(&outline_glyph, FT_RENDER_MODE_NORMAL, nullptr, 1);
      const FT_BitmapGlyph outline_bitmap = reinterpret_cast<FT_BitmapGlyph>(outline_glyph);
      value.outline_left = outline_bitmap->left;
      value.outline_top = outline_bitmap->top;
      value.outline_width = static_cast<int>(outline_bitmap->bitmap.width);
      value.outline_height = static_cast<int>(outline_bitmap->bitmap.rows);
      value.outline_alpha = flattenFtBitmap(outline_bitmap->bitmap);
      FT_Done_Glyph(outline_glyph);
      FT_Stroker_Done(stroker);
    }

    FT_Done_Glyph(fill_glyph);
    return impl_->glyph_cache.emplace(key, std::move(value)).first->second;
  };

  for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
    const LineLayout& line_layout = get_line_layout(lines[line_index]);
    float line_left = static_cast<float>(anchor_region->x + side_padding);
    const float usable_width_f = static_cast<float>(fitted.usable_width);
    if (align_h == "center") {
      line_left += std::max(usable_width_f - line_layout.advance_width, 0.0f) * 0.5f;
    } else if (align_h == "right") {
      line_left += std::max(usable_width_f - line_layout.advance_width, 0.0f);
    }
    const int baseline_y = base_top + static_cast<int>(line_index) * line_height + ascender;
    float pen_x = line_left;
    for (const ShapedGlyph& glyph : line_layout.glyphs) {
      const GlyphCacheValue& glyph_bitmap = build_glyph(glyph.glyph_index);
      const int fill_x = static_cast<int>(std::lround(pen_x + glyph.x_offset)) + glyph_bitmap.fill_left;
      const int fill_y = baseline_y - static_cast<int>(std::lround(glyph.y_offset)) - glyph_bitmap.fill_top;
      const int outline_x = static_cast<int>(std::lround(pen_x + glyph.x_offset)) + glyph_bitmap.outline_left;
      const int outline_y = baseline_y - static_cast<int>(std::lround(glyph.y_offset)) - glyph_bitmap.outline_top;
      if (glyph_bitmap.fill_width > 0 && glyph_bitmap.fill_height > 0) {
        min_x = std::min(min_x, fill_x);
        min_y = std::min(min_y, fill_y);
        max_x = std::max(max_x, fill_x + glyph_bitmap.fill_width);
        max_y = std::max(max_y, fill_y + glyph_bitmap.fill_height);
      }
      if (glyph_bitmap.outline_width > 0 && glyph_bitmap.outline_height > 0) {
        min_x = std::min(min_x, outline_x);
        min_y = std::min(min_y, outline_y);
        max_x = std::max(max_x, outline_x + glyph_bitmap.outline_width);
        max_y = std::max(max_y, outline_y + glyph_bitmap.outline_height);
      }
      draw_glyphs.push_back(DrawGlyph{&glyph_bitmap, fill_x, fill_y, outline_x, outline_y});
      pen_x += glyph.x_advance;
    }
  }

  if (min_x >= max_x || min_y >= max_y) {
    return overlay;
  }
  if (impl_->job.subtitle_clip) {
    min_x = std::max(min_x, anchor_region->x);
    min_y = std::max(min_y, anchor_region->y);
    max_x = std::min(max_x, anchor_region->x + anchor_region->w);
    max_y = std::min(max_y, anchor_region->y + anchor_region->h);
    if (min_x >= max_x || min_y >= max_y) {
      return SubtitleOverlay{};
    }
  }

  overlay.enabled = true;
  overlay.x = std::clamp(min_x, 0, impl_->video_width);
  overlay.y = std::clamp(min_y, 0, impl_->video_height);
  overlay.width = std::max(0, std::min(max_x, impl_->video_width) - overlay.x);
  overlay.height = std::max(0, std::min(max_y, impl_->video_height) - overlay.y);
  overlay.stride = overlay.width;
  overlay.opacity = impl_->job.subtitle_opacity;
  overlay.cue_text = normalized_text;
  const size_t pixel_count = static_cast<size_t>(overlay.width) * static_cast<size_t>(overlay.height);
  overlay.alpha_mask.assign(pixel_count, 0);
  overlay.luma_mask.assign(pixel_count, 0);
  overlay.chroma_u_mask.assign(pixel_count, 128);
  overlay.chroma_v_mask.assign(pixel_count, 128);

  const RgbaColor fill_color = parseHexColor(impl_->job.subtitle_text_color);
  const RgbaColor outline_color = parseHexColor(impl_->job.subtitle_outline_color);
  const RgbaColor shadow_color = outline_color;

  for (const DrawGlyph& draw : draw_glyphs) {
    const GlyphCacheValue& glyph = *draw.bitmap;
    if (!glyph.outline_alpha.empty()) {
      for (int row = 0; row < glyph.outline_height; ++row) {
        for (int col = 0; col < glyph.outline_width; ++col) {
          const size_t index = static_cast<size_t>(row) * static_cast<size_t>(glyph.outline_width) + static_cast<size_t>(col);
          const float alpha = static_cast<float>(glyph.outline_alpha[index]) / 255.0f;
          blendOverlayPixel(overlay, draw.outline_x - overlay.x + col, draw.outline_y - overlay.y + row, alpha, outline_color);
        }
      }
    }
    if (impl_->job.subtitle_shadow > 0 && !glyph.fill_alpha.empty()) {
      for (int row = 0; row < glyph.fill_height; ++row) {
        for (int col = 0; col < glyph.fill_width; ++col) {
          const size_t index = static_cast<size_t>(row) * static_cast<size_t>(glyph.fill_width) + static_cast<size_t>(col);
          const float alpha = static_cast<float>(glyph.fill_alpha[index]) / 255.0f;
          blendOverlayPixel(
              overlay,
              draw.fill_x - overlay.x + col + impl_->job.subtitle_shadow,
              draw.fill_y - overlay.y + row + impl_->job.subtitle_shadow,
              alpha * 0.55f,
              shadow_color);
        }
      }
    }
    for (int row = 0; row < glyph.fill_height; ++row) {
      for (int col = 0; col < glyph.fill_width; ++col) {
        const size_t index = static_cast<size_t>(row) * static_cast<size_t>(glyph.fill_width) + static_cast<size_t>(col);
        const float alpha = static_cast<float>(glyph.fill_alpha[index]) / 255.0f;
        blendOverlayPixel(overlay, draw.fill_x - overlay.x + col, draw.fill_y - overlay.y + row, alpha, fill_color);
      }
    }
  }

  impl_->overlay_cache.emplace(cache_key, overlay);
  return overlay;
#endif
}

}  // namespace video_engine
