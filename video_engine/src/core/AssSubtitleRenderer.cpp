#include "core/AssSubtitleRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/Logger.h"

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

std::vector<char> readBinaryFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  file.seekg(0, std::ios::end);
  const std::streamoff size = file.tellg();
  if (size <= 0) {
    return {};
  }
  file.seekg(0, std::ios::beg);
  std::vector<char> bytes(static_cast<size_t>(size));
  file.read(bytes.data(), size);
  return bytes;
}

void blendPixel(SubtitleOverlay& overlay, int x, int y, uint8_t red, uint8_t green, uint8_t blue, float source_alpha) {
  if (source_alpha <= 0.0f || x < 0 || y < 0 || x >= overlay.width || y >= overlay.height) {
    return;
  }
  const size_t index = static_cast<size_t>(y) * static_cast<size_t>(overlay.stride) + static_cast<size_t>(x);
  const float destination_alpha = static_cast<float>(overlay.alpha_mask[index]) / 255.0f;
  const float output_alpha = source_alpha + destination_alpha * (1.0f - source_alpha);
  if (output_alpha <= 0.0f) {
    return;
  }

  uint8_t y_value = 0;
  uint8_t u_value = 128;
  uint8_t v_value = 128;
  rgbToYuv(red, green, blue, y_value, u_value, v_value);

  const float source_weight = source_alpha / output_alpha;
  const float destination_weight = destination_alpha * (1.0f - source_alpha) / output_alpha;
  overlay.alpha_mask[index] = static_cast<uint8_t>(std::clamp(std::lround(output_alpha * 255.0f), 0l, 255l));
  overlay.luma_mask[index] = static_cast<uint8_t>(std::clamp(
      std::lround(static_cast<float>(y_value) * source_weight + static_cast<float>(overlay.luma_mask[index]) * destination_weight),
      0l,
      255l));
  overlay.chroma_u_mask[index] = static_cast<uint8_t>(std::clamp(
      std::lround(static_cast<float>(u_value) * source_weight + static_cast<float>(overlay.chroma_u_mask[index]) * destination_weight),
      0l,
      255l));
  overlay.chroma_v_mask[index] = static_cast<uint8_t>(std::clamp(
      std::lround(static_cast<float>(v_value) * source_weight + static_cast<float>(overlay.chroma_v_mask[index]) * destination_weight),
      0l,
      255l));
}

}  // namespace

struct AssSubtitleRenderer::Impl {
  RenderJob job;
  int video_width = 0;
  int video_height = 0;
  mutable uint64_t overlay_revision = 0;
  mutable SubtitleOverlay cached_overlay;
#if defined(VIDEO_ENGINE_HAS_LIBASS)
  ASS_Library* library = nullptr;
  ASS_Renderer* renderer = nullptr;
  ASS_Track* track = nullptr;
#endif
};

AssSubtitleRenderer::AssSubtitleRenderer() : impl_(std::make_unique<Impl>()) {
}

AssSubtitleRenderer::~AssSubtitleRenderer() {
#if defined(VIDEO_ENGINE_HAS_LIBASS)
  if (impl_->track != nullptr) {
    ass_free_track(impl_->track);
    impl_->track = nullptr;
  }
  if (impl_->renderer != nullptr) {
    ass_renderer_done(impl_->renderer);
    impl_->renderer = nullptr;
  }
  if (impl_->library != nullptr) {
    ass_library_done(impl_->library);
    impl_->library = nullptr;
  }
#endif
}

void AssSubtitleRenderer::initialize(const RenderJob& job, int video_width, int video_height) {
  available_ = false;
  impl_->job = job;
  impl_->video_width = video_width;
  impl_->video_height = video_height;
  impl_->overlay_revision = 0;
  impl_->cached_overlay = SubtitleOverlay{};
#if defined(VIDEO_ENGINE_HAS_LIBASS)
  if (impl_->track != nullptr) {
    ass_free_track(impl_->track);
    impl_->track = nullptr;
  }
  if (impl_->renderer != nullptr) {
    ass_renderer_done(impl_->renderer);
    impl_->renderer = nullptr;
  }
  if (impl_->library != nullptr) {
    ass_library_done(impl_->library);
    impl_->library = nullptr;
  }
  if (job.subtitle_ass.empty()) {
    return;
  }

  impl_->library = ass_library_init();
  if (impl_->library == nullptr) {
    Logger::warn("libass unavailable: failed to initialize library.");
    return;
  }
  ass_set_extract_fonts(impl_->library, 1);
  if (!job.subtitle_font_path.empty()) {
    std::vector<char> font_bytes = readBinaryFile(job.subtitle_font_path);
    if (!font_bytes.empty()) {
      ass_add_font(impl_->library, job.subtitle_font_path.c_str(), font_bytes.data(), static_cast<int>(font_bytes.size()));
    }
  }

  impl_->renderer = ass_renderer_init(impl_->library);
  if (impl_->renderer == nullptr) {
    Logger::warn("libass unavailable: failed to initialize renderer.");
    return;
  }
  ass_set_frame_size(impl_->renderer, video_width, video_height);
  ass_set_storage_size(impl_->renderer, video_width, video_height);
  ass_set_fonts(
      impl_->renderer,
      job.subtitle_font_path.empty() ? nullptr : job.subtitle_font_path.c_str(),
      job.subtitle_font_family.empty() ? "Arial" : job.subtitle_font_family.c_str(),
      ASS_FONTPROVIDER_AUTODETECT,
      nullptr,
      1);

  impl_->track = ass_read_file(impl_->library, const_cast<char*>(job.subtitle_ass.c_str()), nullptr);
  if (impl_->track == nullptr) {
    Logger::warn("Failed to load ASS subtitle file.");
    return;
  }

  Logger::info("Using libass subtitle renderer.");
  available_ = true;
#else
  (void)job;
  (void)video_width;
  (void)video_height;
  Logger::warn("This build was compiled without libass support.");
#endif
}

SubtitleOverlay AssSubtitleRenderer::render(double timestamp_seconds) const {
  SubtitleOverlay overlay;
#if !defined(VIDEO_ENGINE_HAS_LIBASS)
  (void)timestamp_seconds;
  return overlay;
#else
  if (!available_ || impl_->renderer == nullptr || impl_->track == nullptr) {
    return overlay;
  }

  int change = 2;
  ASS_Image* images = ass_render_frame(
      impl_->renderer,
      impl_->track,
      static_cast<long long>(std::llround(timestamp_seconds * 1000.0)),
      &change);
  if (change == 0) {
    return impl_->cached_overlay;
  }
  if (images == nullptr) {
    impl_->cached_overlay = SubtitleOverlay{};
    return overlay;
  }

  int min_x = impl_->video_width;
  int min_y = impl_->video_height;
  int max_x = 0;
  int max_y = 0;
  for (ASS_Image* image = images; image != nullptr; image = image->next) {
    if (image->w <= 0 || image->h <= 0 || image->bitmap == nullptr) {
      continue;
    }
    min_x = std::min(min_x, std::clamp(image->dst_x, 0, impl_->video_width));
    min_y = std::min(min_y, std::clamp(image->dst_y, 0, impl_->video_height));
    max_x = std::max(max_x, std::clamp(image->dst_x + image->w, 0, impl_->video_width));
    max_y = std::max(max_y, std::clamp(image->dst_y + image->h, 0, impl_->video_height));
  }
  if (min_x >= max_x || min_y >= max_y) {
    impl_->cached_overlay = SubtitleOverlay{};
    return overlay;
  }

  overlay.enabled = true;
  overlay.x = min_x;
  overlay.y = min_y;
  overlay.width = max_x - min_x;
  overlay.height = max_y - min_y;
  overlay.stride = overlay.width;
  overlay.opacity = impl_->job.subtitle_opacity;
  ++impl_->overlay_revision;
  overlay.cue_text = impl_->job.subtitle_ass + "#" + std::to_string(impl_->overlay_revision);
  const size_t pixel_count = static_cast<size_t>(overlay.width) * static_cast<size_t>(overlay.height);
  overlay.alpha_mask.assign(pixel_count, 0);
  overlay.luma_mask.assign(pixel_count, 0);
  overlay.chroma_u_mask.assign(pixel_count, 128);
  overlay.chroma_v_mask.assign(pixel_count, 128);

  for (ASS_Image* image = images; image != nullptr; image = image->next) {
    const uint8_t red = static_cast<uint8_t>((image->color >> 24) & 0xFF);
    const uint8_t green = static_cast<uint8_t>((image->color >> 16) & 0xFF);
    const uint8_t blue = static_cast<uint8_t>((image->color >> 8) & 0xFF);
    const float image_alpha = static_cast<float>(255 - (image->color & 0xFF)) / 255.0f;
    if (image_alpha <= 0.0f) {
      continue;
    }
    for (int row = 0; row < image->h; ++row) {
      const uint8_t* source = image->bitmap + static_cast<size_t>(row) * static_cast<size_t>(image->stride);
      const int target_y = image->dst_y + row - overlay.y;
      for (int col = 0; col < image->w; ++col) {
        const float alpha = image_alpha * static_cast<float>(source[col]) / 255.0f * impl_->job.subtitle_opacity;
        blendPixel(overlay, image->dst_x + col - overlay.x, target_y, red, green, blue, alpha);
      }
    }
  }

  impl_->cached_overlay = overlay;
  return overlay;
#endif
}

}  // namespace video_engine
