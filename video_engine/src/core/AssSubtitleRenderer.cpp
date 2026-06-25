#include "core/AssSubtitleRenderer.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/SrtParser.h"

#if defined(VIDEO_ENGINE_HAS_LIBASS)
extern "C" {
#include <ass/ass.h>
}
#endif
#if defined(VIDEO_ENGINE_HAS_HARFBUZZ)
#include <hb.h>
#endif

namespace video_engine {

namespace {

void rgbToYuv(uint8_t red, uint8_t green, uint8_t blue, uint8_t& y, uint8_t& u, uint8_t& v) {
  const float yf = 0.299f * red + 0.587f * green + 0.114f * blue;
  const float uf = -0.168736f * red - 0.331264f * green + 0.5f * blue + 128.0f;
  const float vf = 0.5f * red - 0.418688f * green - 0.081312f * blue + 128.0f;
  y = static_cast<uint8_t>(std::clamp(yf, 0.0f, 255.0f));
  u = static_cast<uint8_t>(std::clamp(uf, 0.0f, 255.0f));
  v = static_cast<uint8_t>(std::clamp(vf, 0.0f, 255.0f));
}

std::string escapeAssText(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size() * 2);
  for (char ch : text) {
    if (ch == '\n') {
      escaped += "\\N";
    } else if (ch == '{' || ch == '}') {
      escaped.push_back('\\');
      escaped.push_back(ch);
    } else if (ch != '\r') {
      escaped.push_back(ch);
    }
  }
  return escaped;
}

std::vector<char> readBinaryFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open font file: " + path);
  }

  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  input.seekg(0, std::ios::beg);
  if (size <= 0) {
    throw std::runtime_error("Font file is empty: " + path);
  }

  std::vector<char> data(static_cast<size_t>(size));
  if (!input.read(data.data(), size)) {
    throw std::runtime_error("Failed to read font file: " + path);
  }
  return data;
}

std::string fontAttachmentName(const RenderJob& job) {
  if (job.subtitle_font_path.empty()) {
    return {};
  }

  const std::filesystem::path path(job.subtitle_font_path);
  if (path.has_filename()) {
    return path.filename().string();
  }
  return path.string();
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
#if defined(VIDEO_ENGINE_HAS_HARFBUZZ)
  return static_cast<char32_t>(hb_unicode_toupper(hb_unicode_funcs_get_default(), static_cast<hb_codepoint_t>(code_point)));
#else
  if (code_point >= U'a' && code_point <= U'z') {
    return code_point - (U'a' - U'A');
  }
  return code_point;
#endif
}

size_t countUtf8CodePoints(const std::string& text) {
  size_t count = 0;
  for (size_t index = 0; index < text.size();) {
    decodeUtf8CodePoint(text, index);
    ++count;
  }
  return count;
}

std::string uppercaseUnicode(const std::string& text) {
  std::string output;
  output.reserve(text.size());
  for (size_t index = 0; index < text.size();) {
    appendUtf8CodePoint(output, uppercaseCodePoint(decodeUtf8CodePoint(text, index)));
  }
  return output;
}

std::string formatAssTime(double seconds) {
  const int total_centiseconds = static_cast<int>(std::llround(seconds * 100.0));
  const int hours = total_centiseconds / 360000;
  const int minutes = (total_centiseconds % 360000) / 6000;
  const int secs = (total_centiseconds % 6000) / 100;
  const int centis = total_centiseconds % 100;

  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d.%02d", hours, minutes, secs, centis);
  return std::string(buffer);
}

std::string toAssColor(const std::string& hex) {
  const auto parse_component = [&](size_t offset) {
    return static_cast<unsigned int>(std::stoul(hex.substr(offset, 2), nullptr, 16));
  };

  const unsigned int red = parse_component(1);
  const unsigned int green = parse_component(3);
  const unsigned int blue = parse_component(5);
  unsigned int alpha = 0;
  if (hex.size() == 9) {
    const unsigned int rgba_alpha = parse_component(7);
    alpha = 255U - rgba_alpha;
  }

  std::ostringstream stream;
  stream << "&H" << std::uppercase << std::setfill('0') << std::hex << std::setw(2) << alpha << std::setw(2)
         << blue << std::setw(2) << green << std::setw(2) << red;
  return stream.str();
}

std::vector<std::string> wrapWordsToLines(const std::string& text, int max_chars_per_line) {
  std::vector<std::string> output;
  std::istringstream input(text);
  std::string raw_line;
  while (std::getline(input, raw_line, '\n')) {
    std::istringstream words(raw_line);
    std::string word;
    std::string current;
    while (words >> word) {
      if (static_cast<int>(countUtf8CodePoints(word)) > max_chars_per_line && !current.empty()) {
        output.push_back(current);
        current.clear();
      }
      const std::string candidate = current.empty() ? word : current + " " + word;
      if (static_cast<int>(countUtf8CodePoints(candidate)) > max_chars_per_line && !current.empty()) {
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

int computeHorizontalSafetyPadding(int margin, int font_pixels, int outline, bool italic) {
  const int outline_padding = std::max(outline * 3, 6);
  const int italic_padding = italic ? std::max(font_pixels / 3, 10) : std::max(font_pixels / 6, 4);
  return std::max(margin * 2, outline_padding + italic_padding);
}

float estimateCodePointWidthEm(char32_t code_point, bool bold, bool italic) {
  float width = 0.62f;
  if (code_point == U' ') {
    width = 0.34f;
  } else if ((code_point >= U'0' && code_point <= U'9') || (code_point >= U'A' && code_point <= U'Z')) {
    width = 0.72f;
  } else if (code_point == U',' || code_point == U'.' || code_point == U':' || code_point == U';' || code_point == U'!' ||
             code_point == U'?' || code_point == U'\'') {
    width = 0.30f;
  } else if (code_point == U'-' || code_point == U'_') {
    width = 0.42f;
  } else if (code_point >= 0x2E80) {
    width = 1.0f;
  }

  if (bold) {
    width += 0.04f;
  }
  if (italic) {
    width += 0.03f;
  }
  return width;
}

float estimateTextWidthPixels(const std::string& text, int font_pixels, int outline, bool bold, bool italic) {
  float width = 0.0f;
  for (size_t index = 0; index < text.size();) {
    width += estimateCodePointWidthEm(decodeUtf8CodePoint(text, index), bold, italic) * static_cast<float>(font_pixels);
  }
  width += static_cast<float>(outline) * 2.5f;
  return width;
}

std::vector<std::string> wrapCueTextToLines(
    const std::string& text,
    int region_width,
    int margin,
    int font_pixels,
    int outline,
    bool bold,
    bool italic) {
  const int side_safety_padding = computeHorizontalSafetyPadding(margin, font_pixels, outline, italic);
  const float usable_width = static_cast<float>(std::max(region_width - side_safety_padding * 2, font_pixels * 4));
  std::vector<std::string> output;

  std::istringstream input(text);
  std::string raw_line;
  while (std::getline(input, raw_line, '\n')) {
    std::istringstream words(raw_line);
    std::string word;
    std::string current;
    while (words >> word) {
      const std::string candidate = current.empty() ? word : current + " " + word;
      if (!current.empty() && estimateTextWidthPixels(candidate, font_pixels, outline, bold, italic) > usable_width) {
        output.push_back(current);
        current = word;
      } else {
        current = candidate;
      }
    }

    if (!current.empty()) {
      output.push_back(current);
    } else if (raw_line.empty()) {
      output.emplace_back();
    }
  }

  if (output.empty()) {
    output.push_back(text);
  }
  return output;
}

struct AssMargins {
  int left = 20;
  int right = 20;
  int vertical = 8;
};

AssMargins computeAssMargins(
    const RenderJob& job,
    const std::optional<Region>& wrap_region,
    int video_width,
    int video_height,
    int font_pixels) {
  AssMargins margins;
  margins.vertical = job.subtitle_margin;

  if (!wrap_region.has_value()) {
    return margins;
  }

  const int side_safety_padding = computeHorizontalSafetyPadding(
      job.subtitle_margin,
      font_pixels,
      job.subtitle_outline,
      job.subtitle_italic);
  margins.left = std::max(0, wrap_region->x + side_safety_padding);
  margins.right = std::max(0, video_width - (wrap_region->x + wrap_region->w) + side_safety_padding);
  margins.vertical = std::max(0, video_height - (wrap_region->y + wrap_region->h) + job.subtitle_margin);
  return margins;
}

bool shouldUseAbsoluteAssPositioning(const RenderJob& job, const std::optional<Region>& wrap_region) {
  return wrap_region.has_value() && (!job.subtitle_srt.empty() || !job.subtitle_text.empty());
}

std::string buildAssScript(
    const RenderJob& job,
    const std::vector<SubtitleCue>& cues,
    int video_width,
    int video_height,
    int subtitle_font_pixels,
    const std::optional<Region>& wrap_region) {
  std::string script;
  script.reserve(4096);
  script += "[Script Info]\n";
  script += "ScriptType: v4.00+\n";
  script += "WrapStyle: 2\n";
  script += "PlayResX: " + std::to_string(video_width) + "\n";
  script += "PlayResY: " + std::to_string(video_height) + "\n";
  script += "[V4+ Styles]\n";
  script += "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
            "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
            "Alignment, MarginL, MarginR, MarginV, Encoding\n";
  const std::string text_color = toAssColor(job.subtitle_text_color);
  const std::string outline_color = toAssColor(job.subtitle_outline_color);
  const std::string back_color = toAssColor(job.subtitle_back_color);
  const AssMargins ass_margins = computeAssMargins(job, wrap_region, video_width, video_height, subtitle_font_pixels);
  const bool use_region_layout = wrap_region.has_value();
  script += "Style: Default," + job.subtitle_font_family + "," + std::to_string(subtitle_font_pixels) +
            "," + text_color + ",&H000000FF," + outline_color + "," + back_color + "," +
            (job.subtitle_bold ? "-1" : "0") + "," +
            (job.subtitle_italic ? "-1" : "0") + ",0,0,100,100,0,0,1," + std::to_string(job.subtitle_outline) +
            "," + std::to_string(job.subtitle_shadow) + ",2," + std::to_string(ass_margins.left) + "," +
            std::to_string(ass_margins.right) + "," + std::to_string(ass_margins.vertical) + ",1\n";
  script += "[Events]\n";
  script += "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
  for (const SubtitleCue& cue : cues) {
    std::string normalized_text = cue.text;
    if (job.subtitle_uppercase) {
      normalized_text = uppercaseUnicode(normalized_text);
    }
    if (use_region_layout) {
      const std::vector<std::string> lines = wrapCueTextToLines(
          normalized_text,
          wrap_region->w,
          job.subtitle_margin,
          subtitle_font_pixels,
          job.subtitle_outline,
          job.subtitle_bold,
          job.subtitle_italic);
      const int line_height = std::max(
          static_cast<int>(std::lround(static_cast<float>(subtitle_font_pixels) * 1.18f)) + job.subtitle_outline * 2 +
              job.subtitle_shadow,
          subtitle_font_pixels + 4);
      const int total_height = static_cast<int>(lines.size()) * line_height;
      const int top_padding = std::max(job.subtitle_margin, job.subtitle_outline * 2);
      const int available_height = std::max(wrap_region->h - top_padding * 2, line_height);
      const int top_y = wrap_region->y + top_padding + std::max(available_height - total_height, 0) / 2;
      const int center_x = wrap_region->x + wrap_region->w / 2;
      for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const int line_y = top_y + static_cast<int>(line_index) * line_height;
        script += "Dialogue: 0," + formatAssTime(cue.start) + "," + formatAssTime(cue.end) +
                  ",Default,,0,0,0,,{\\an8\\q2\\pos(" + std::to_string(center_x) + "," + std::to_string(line_y) +
                  ")}" + escapeAssText(lines[line_index]) + "\n";
      }
    } else {
      script += "Dialogue: 0," + formatAssTime(cue.start) + "," + formatAssTime(cue.end) +
                ",Default,," + std::to_string(ass_margins.left) + "," + std::to_string(ass_margins.right) + "," +
                std::to_string(ass_margins.vertical) + ",,{\\q2}" + escapeAssText(normalized_text) + "\n";
    }
  }
  return script;
}

}  // namespace

struct AssSubtitleRenderer::Impl {
#if defined(VIDEO_ENGINE_HAS_LIBASS)
  ASS_Library* library = nullptr;
  ASS_Renderer* renderer = nullptr;
  ASS_Track* track = nullptr;
#endif
  RenderJob job;
  int video_width = 0;
  int video_height = 0;
  int subtitle_font_pixels = 0;
  bool use_absolute_ass_positioning = false;
};

AssSubtitleRenderer::AssSubtitleRenderer() : impl_(std::make_unique<Impl>()) {
}

AssSubtitleRenderer::~AssSubtitleRenderer() {
#if defined(VIDEO_ENGINE_HAS_LIBASS)
  if (impl_->track) {
    ass_free_track(impl_->track);
    impl_->track = nullptr;
  }
  if (impl_->renderer) {
    ass_renderer_done(impl_->renderer);
    impl_->renderer = nullptr;
  }
  if (impl_->library) {
    ass_library_done(impl_->library);
    impl_->library = nullptr;
  }
#endif
}

void AssSubtitleRenderer::initialize(const RenderJob& job, int video_width, int video_height) {
  impl_->job = job;
  impl_->video_width = video_width;
  impl_->video_height = video_height;
  impl_->subtitle_font_pixels = job.resolveSubtitleFontPixels(video_height);
  available_ = false;

#if !defined(VIDEO_ENGINE_HAS_LIBASS)
  return;
#else
  if (job.subtitle_srt.empty() && job.subtitle_text.empty()) {
    return;
  }

  impl_->library = ass_library_init();
  if (!impl_->library) {
    throw std::runtime_error("Failed to initialize libass library.");
  }
  impl_->renderer = ass_renderer_init(impl_->library);
  if (!impl_->renderer) {
    throw std::runtime_error("Failed to initialize libass renderer.");
  }

  ass_set_frame_size(impl_->renderer, video_width, video_height);
  ass_set_storage_size(impl_->renderer, video_width, video_height);

  std::vector<SubtitleCue> cues;
  if (!job.subtitle_srt.empty()) {
    cues = SrtParser::parseFile(job.subtitle_srt);
  } else if (!job.subtitle_text.empty()) {
    cues.push_back(SubtitleCue{0.0, 1.0e12, job.subtitle_text});
  }

  const std::optional<Region> wrap_region = job.regions.empty() ? std::nullopt : std::optional<Region>(job.regions.front());
  impl_->use_absolute_ass_positioning = shouldUseAbsoluteAssPositioning(job, wrap_region);
  const std::string ass_script = buildAssScript(
      job,
      cues,
      video_width,
      video_height,
      impl_->subtitle_font_pixels,
      wrap_region);
  impl_->track = ass_read_memory(
      impl_->library,
      reinterpret_cast<char*>(const_cast<char*>(ass_script.data())),
      static_cast<size_t>(ass_script.size()),
      nullptr);

  if (!impl_->track) {
    throw std::runtime_error("Failed to load subtitles into libass track.");
  }

  std::vector<char> attached_font_data;
  if (!job.subtitle_font_path.empty()) {
    attached_font_data = readBinaryFile(job.subtitle_font_path);
    const std::string attachment_name = fontAttachmentName(job);
    ass_add_font(
        impl_->library,
        attachment_name.empty() ? job.subtitle_font_path.c_str() : attachment_name.c_str(),
        attached_font_data.data(),
        static_cast<int>(attached_font_data.size()));
  }

  ass_set_fonts(
      impl_->renderer,
      job.subtitle_font_path.empty() ? nullptr : job.subtitle_font_path.c_str(),
      job.subtitle_font_family.empty() ? "Noto Sans" : job.subtitle_font_family.c_str(),
      1,
      nullptr,
      1);

  if (impl_->track->n_styles > 0) {
    ASS_Style& style = impl_->track->styles[0];
    style.FontSize = impl_->subtitle_font_pixels;
    style.Bold = job.subtitle_bold ? -1 : 0;
    style.Italic = job.subtitle_italic ? -1 : 0;
    style.Outline = job.subtitle_outline;
    style.Shadow = job.subtitle_shadow;
    style.MarginV = job.subtitle_margin;
    if (!job.subtitle_font_family.empty()) {
      style.FontName = strdup(job.subtitle_font_family.c_str());
    }
  }

  available_ = true;
#endif
}

SubtitleOverlay AssSubtitleRenderer::render(double timestamp_seconds, const Region* anchor_region) const {
  SubtitleOverlay overlay;
#if !defined(VIDEO_ENGINE_HAS_LIBASS)
  (void)timestamp_seconds;
  (void)anchor_region;
  return overlay;
#else
  if (!available_ || anchor_region == nullptr || anchor_region->w <= 0 || anchor_region->h <= 0) {
    return overlay;
  }

  int detect_change = 0;
  ASS_Image* images =
      ass_render_frame(impl_->renderer, impl_->track, static_cast<long long>(std::llround(timestamp_seconds * 1000.0)),
                       &detect_change);
  if (!images) {
    return overlay;
  }

  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int max_y = std::numeric_limits<int>::min();
  for (ASS_Image* image = images; image != nullptr; image = image->next) {
    min_x = std::min(min_x, image->dst_x);
    min_y = std::min(min_y, image->dst_y);
    max_x = std::max(max_x, image->dst_x + image->w);
    max_y = std::max(max_y, image->dst_y + image->h);
  }
  if (min_x >= max_x || min_y >= max_y) {
    return overlay;
  }

  overlay.width = max_x - min_x;
  overlay.height = max_y - min_y;
  overlay.stride = overlay.width;
  if (impl_->use_absolute_ass_positioning) {
    overlay.x = std::clamp(min_x, 0, std::max(impl_->video_width - overlay.width, 0));
    overlay.y = std::clamp(min_y, 0, std::max(impl_->video_height - overlay.height, 0));
  } else {
    overlay.x = std::clamp(
        anchor_region->x + std::max(anchor_region->w - overlay.width, 0) / 2,
        0,
        std::max(impl_->video_width - overlay.width, 0));
    overlay.y = std::clamp(
        anchor_region->y + std::max(anchor_region->h - overlay.height, 0) / 2,
        0,
        std::max(impl_->video_height - overlay.height, 0));
  }
  overlay.opacity = impl_->job.subtitle_opacity;
  overlay.enabled = overlay.width > 0 && overlay.height > 0;
  if (!overlay.enabled) {
    return overlay;
  }

  const size_t pixel_count = static_cast<size_t>(overlay.width) * static_cast<size_t>(overlay.height);
  overlay.alpha_mask.assign(pixel_count, 0);
  overlay.luma_mask.assign(pixel_count, 0);
  overlay.chroma_u_mask.assign(pixel_count, 128);
  overlay.chroma_v_mask.assign(pixel_count, 128);

  std::vector<float> alpha(pixel_count, 0.0f);
  std::vector<float> red(pixel_count, 0.0f);
  std::vector<float> green(pixel_count, 0.0f);
  std::vector<float> blue(pixel_count, 0.0f);

  for (ASS_Image* image = images; image != nullptr; image = image->next) {
    const float image_alpha = (255.0f - static_cast<float>(image->color & 0xFF)) / 255.0f;
    const float image_blue = static_cast<float>((image->color >> 8) & 0xFF) / 255.0f;
    const float image_green = static_cast<float>((image->color >> 16) & 0xFF) / 255.0f;
    const float image_red = static_cast<float>((image->color >> 24) & 0xFF) / 255.0f;

    for (int y = 0; y < image->h; ++y) {
      for (int x = 0; x < image->w; ++x) {
        const int dst_x = image->dst_x - min_x + x;
        const int dst_y = image->dst_y - min_y + y;
        if (dst_x < 0 || dst_x >= overlay.width || dst_y < 0 || dst_y >= overlay.height) {
          continue;
        }

        const size_t index =
            static_cast<size_t>(dst_y) * static_cast<size_t>(overlay.stride) + static_cast<size_t>(dst_x);
        const float coverage = static_cast<float>(image->bitmap[y * image->stride + x]) / 255.0f * image_alpha;
        if (coverage <= 0.0f) {
          continue;
        }

        const float inv = 1.0f - coverage;
        alpha[index] = coverage + alpha[index] * inv;
        red[index] = image_red * coverage + red[index] * inv;
        green[index] = image_green * coverage + green[index] * inv;
        blue[index] = image_blue * coverage + blue[index] * inv;
      }
    }
  }

  for (size_t index = 0; index < pixel_count; ++index) {
    if (alpha[index] <= 0.0f) {
      continue;
    }
    const float inv_alpha = alpha[index] > 0.0f ? 1.0f / alpha[index] : 0.0f;
    const uint8_t r = static_cast<uint8_t>(std::clamp(red[index] * inv_alpha * 255.0f, 0.0f, 255.0f));
    const uint8_t g = static_cast<uint8_t>(std::clamp(green[index] * inv_alpha * 255.0f, 0.0f, 255.0f));
    const uint8_t b = static_cast<uint8_t>(std::clamp(blue[index] * inv_alpha * 255.0f, 0.0f, 255.0f));
    overlay.alpha_mask[index] = static_cast<uint8_t>(std::clamp(alpha[index] * 255.0f, 0.0f, 255.0f));
    rgbToYuv(r, g, b, overlay.luma_mask[index], overlay.chroma_u_mask[index], overlay.chroma_v_mask[index]);
  }

  return overlay;
#endif
}

}  // namespace video_engine
