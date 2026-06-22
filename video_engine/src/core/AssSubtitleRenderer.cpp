#include "core/AssSubtitleRenderer.h"

#include <algorithm>
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
      while (static_cast<int>(word.size()) > max_chars_per_line && max_chars_per_line > 1) {
        const std::string chunk = word.substr(0, static_cast<size_t>(max_chars_per_line - 1)) + "-";
        if (!current.empty()) {
          output.push_back(current);
          current.clear();
        }
        output.push_back(chunk);
        word.erase(0, static_cast<size_t>(max_chars_per_line - 1));
      }

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

std::string wrapCueText(const std::string& text, int region_width, int margin, int font_pixels) {
  const int usable_width = std::max(region_width - margin * 2, font_pixels * 4);
  const float estimated_char_width = std::max(static_cast<float>(font_pixels) * 0.58f, 1.0f);
  const int max_chars_per_line = std::max(1, static_cast<int>(static_cast<float>(usable_width) / estimated_char_width));
  const std::vector<std::string> lines = wrapWordsToLines(text, max_chars_per_line);
  std::ostringstream wrapped;
  for (size_t index = 0; index < lines.size(); ++index) {
    if (index > 0) {
      wrapped << "\n";
    }
    wrapped << lines[index];
  }
  return wrapped.str();
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
  script += "PlayResX: " + std::to_string(video_width) + "\n";
  script += "PlayResY: " + std::to_string(video_height) + "\n";
  script += "[V4+ Styles]\n";
  script += "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
            "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
            "Alignment, MarginL, MarginR, MarginV, Encoding\n";
  const std::string text_color = toAssColor(job.subtitle_text_color);
  const std::string outline_color = toAssColor(job.subtitle_outline_color);
  const std::string back_color = toAssColor(job.subtitle_back_color);
  script += "Style: Default," + job.subtitle_font_family + "," + std::to_string(subtitle_font_pixels) +
            "," + text_color + ",&H000000FF," + outline_color + "," + back_color + "," +
            (job.subtitle_bold ? "-1" : "0") + "," +
            (job.subtitle_italic ? "-1" : "0") + ",0,0,100,100,0,0,1," + std::to_string(job.subtitle_outline) +
            "," + std::to_string(job.subtitle_shadow) + ",2,20,20," + std::to_string(job.subtitle_margin) + ",1\n";
  script += "[Events]\n";
  script += "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
  for (const SubtitleCue& cue : cues) {
    const std::string wrapped_text =
        wrap_region.has_value() ? wrapCueText(cue.text, wrap_region->w, job.subtitle_margin, subtitle_font_pixels) : cue.text;
    script += "Dialogue: 0," + formatAssTime(cue.start) + "," + formatAssTime(cue.end) +
              ",Default,,0,0," + std::to_string(job.subtitle_margin) + ",," + escapeAssText(wrapped_text) + "\n";
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
  ass_set_fonts(
      impl_->renderer,
      job.subtitle_font_path.empty() ? nullptr : job.subtitle_font_path.c_str(),
      job.subtitle_font_family.empty() ? "Noto Sans" : job.subtitle_font_family.c_str(),
      1,
      nullptr,
      1);

  std::vector<SubtitleCue> cues;
  if (!job.subtitle_srt.empty()) {
    cues = SrtParser::parseFile(job.subtitle_srt);
  } else if (!job.subtitle_text.empty()) {
    cues.push_back(SubtitleCue{0.0, 1.0e12, job.subtitle_text});
  }

  const std::optional<Region> wrap_region = job.regions.empty() ? std::nullopt : std::optional<Region>(job.regions.front());
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
  overlay.x = std::clamp(
      anchor_region->x + std::max(anchor_region->w - overlay.width, 0) / 2,
      0,
      std::max(impl_->video_width - overlay.width, 0));
  overlay.y = std::clamp(
      anchor_region->y + std::max(anchor_region->h - overlay.height, 0) / 2,
      0,
      std::max(impl_->video_height - overlay.height, 0));
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
