#include "core/OverlayLayerRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/SubtitleRenderer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

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

struct DecodedRgbaImage {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;
};

DecodedRgbaImage decodeImageToRgba(const std::string& path) {
  AVFormatContext* format_context = nullptr;
  AVCodecContext* codec_context = nullptr;
  AVFrame* decoded_frame = nullptr;
  AVFrame* rgba_frame = nullptr;
  AVPacket* packet = nullptr;
  SwsContext* sws_context = nullptr;

  auto cleanup = [&]() {
    sws_freeContext(sws_context);
    av_packet_free(&packet);
    av_frame_free(&rgba_frame);
    av_frame_free(&decoded_frame);
    avcodec_free_context(&codec_context);
    avformat_close_input(&format_context);
  };

  if (avformat_open_input(&format_context, path.c_str(), nullptr, nullptr) < 0) {
    throw std::runtime_error("Failed to open logo image: " + path);
  }
  if (avformat_find_stream_info(format_context, nullptr) < 0) {
    cleanup();
    throw std::runtime_error("Failed to read logo image stream info.");
  }

  const int stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (stream_index < 0) {
    cleanup();
    throw std::runtime_error("Failed to find a decodable logo image stream.");
  }

  const AVStream* stream = format_context->streams[stream_index];
  const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec) {
    cleanup();
    throw std::runtime_error("No decoder available for logo image.");
  }

  codec_context = avcodec_alloc_context3(codec);
  decoded_frame = av_frame_alloc();
  rgba_frame = av_frame_alloc();
  packet = av_packet_alloc();
  if (!codec_context || !decoded_frame || !rgba_frame || !packet) {
    cleanup();
    throw std::runtime_error("Failed to allocate FFmpeg resources for logo image.");
  }

  if (avcodec_parameters_to_context(codec_context, stream->codecpar) < 0 ||
      avcodec_open2(codec_context, codec, nullptr) < 0) {
    cleanup();
    throw std::runtime_error("Failed to open logo decoder.");
  }

  bool got_frame = false;
  while (!got_frame && av_read_frame(format_context, packet) >= 0) {
    if (packet->stream_index != stream_index) {
      av_packet_unref(packet);
      continue;
    }
    if (avcodec_send_packet(codec_context, packet) >= 0) {
      got_frame = avcodec_receive_frame(codec_context, decoded_frame) >= 0;
    }
    av_packet_unref(packet);
  }
  if (!got_frame) {
    avcodec_send_packet(codec_context, nullptr);
    got_frame = avcodec_receive_frame(codec_context, decoded_frame) >= 0;
  }
  if (!got_frame) {
    cleanup();
    throw std::runtime_error("Failed to decode logo image frame.");
  }

  const int buffer_size =
      av_image_get_buffer_size(AV_PIX_FMT_RGBA, decoded_frame->width, decoded_frame->height, 1);
  DecodedRgbaImage image;
  image.width = decoded_frame->width;
  image.height = decoded_frame->height;
  image.pixels.resize(static_cast<size_t>(buffer_size));

  uint8_t* dest_data[4] = {};
  int dest_linesize[4] = {};
  av_image_fill_arrays(dest_data, dest_linesize, image.pixels.data(), AV_PIX_FMT_RGBA, image.width, image.height, 1);
  sws_context = sws_getContext(
      image.width,
      image.height,
      static_cast<AVPixelFormat>(decoded_frame->format),
      image.width,
      image.height,
      AV_PIX_FMT_RGBA,
      SWS_BILINEAR,
      nullptr,
      nullptr,
      nullptr);
  if (!sws_context) {
    cleanup();
    throw std::runtime_error("Failed to create swscale context for logo image.");
  }

  sws_scale(
      sws_context,
      decoded_frame->data,
      decoded_frame->linesize,
      0,
      image.height,
      dest_data,
      dest_linesize);

  cleanup();
  return image;
}

DecodedRgbaImage scaleImageNearest(const DecodedRgbaImage& source, int target_width, int target_height) {
  DecodedRgbaImage scaled;
  scaled.width = std::max(1, target_width);
  scaled.height = std::max(1, target_height);
  scaled.pixels.resize(static_cast<size_t>(scaled.width) * static_cast<size_t>(scaled.height) * 4U, 0);

  for (int y = 0; y < scaled.height; ++y) {
    const int source_y = std::clamp(y * source.height / scaled.height, 0, source.height - 1);
    for (int x = 0; x < scaled.width; ++x) {
      const int source_x = std::clamp(x * source.width / scaled.width, 0, source.width - 1);
      const size_t src_index = (static_cast<size_t>(source_y) * static_cast<size_t>(source.width) +
                                static_cast<size_t>(source_x)) *
                               4U;
      const size_t dst_index =
          (static_cast<size_t>(y) * static_cast<size_t>(scaled.width) + static_cast<size_t>(x)) * 4U;
      for (int channel = 0; channel < 4; ++channel) {
        scaled.pixels[dst_index + static_cast<size_t>(channel)] = source.pixels[src_index + static_cast<size_t>(channel)];
      }
    }
  }

  return scaled;
}

int reflectPosition(double value, int travel) {
  if (travel <= 0) {
    return 0;
  }
  const double period = static_cast<double>(travel) * 2.0;
  double wrapped = std::fmod(value, period);
  if (wrapped < 0.0) {
    wrapped += period;
  }
  if (wrapped > static_cast<double>(travel)) {
    wrapped = period - wrapped;
  }
  return static_cast<int>(std::llround(wrapped));
}

std::pair<int, int> computeMotionPosition(
    int video_width,
    int video_height,
    int object_width,
    int object_height,
    int margin,
    bool bounce,
    float speed_x,
    float speed_y,
    double timestamp_seconds,
    int seed_x,
    int seed_y) {
  const int bounded_width = std::min(object_width, std::max(video_width - margin * 2, 1));
  const int bounded_height = std::min(object_height, std::max(video_height - margin * 2, 1));
  const int max_x = std::max(video_width - bounded_width - margin * 2, 0);
  const int max_y = std::max(video_height - bounded_height - margin * 2, 0);
  if (bounce) {
    return std::make_pair(
        margin + reflectPosition(static_cast<double>(seed_x) + static_cast<double>(speed_x) * timestamp_seconds, max_x),
        margin + reflectPosition(static_cast<double>(seed_y) + static_cast<double>(speed_y) * timestamp_seconds, max_y));
  }
  return std::make_pair(std::max(video_width - bounded_width - margin, 0), margin);
}

Region makeMotionRegion(
    int video_width,
    int video_height,
    int box_width,
    int box_height,
    int margin,
    bool bounce,
    float speed_x,
    float speed_y,
    double timestamp_seconds,
    int seed_x,
    int seed_y) {
  Region region;
  region.w = std::min(box_width, std::max(video_width - margin * 2, 1));
  region.h = std::min(box_height, std::max(video_height - margin * 2, 1));
  const auto [motion_x, motion_y] = computeMotionPosition(
      video_width, video_height, region.w, region.h, margin, bounce, speed_x, speed_y, timestamp_seconds, seed_x, seed_y);
  region.x = motion_x;
  region.y = motion_y;
  return region;
}

SubtitleOverlay logoToOverlay(const DecodedRgbaImage& logo, const Region& region, float opacity) {
  SubtitleOverlay overlay;
  if (logo.width <= 0 || logo.height <= 0) {
    return overlay;
  }

  overlay.enabled = true;
  overlay.x = region.x;
  overlay.y = region.y;
  overlay.width = logo.width;
  overlay.height = logo.height;
  overlay.stride = logo.width;
  overlay.opacity = std::clamp(opacity, 0.0f, 1.0f);
  const size_t pixel_count = static_cast<size_t>(logo.width) * static_cast<size_t>(logo.height);
  overlay.alpha_mask.resize(pixel_count, 0);
  overlay.luma_mask.resize(pixel_count, 0);
  overlay.chroma_u_mask.resize(pixel_count, 128);
  overlay.chroma_v_mask.resize(pixel_count, 128);

  for (size_t index = 0; index < pixel_count; ++index) {
    const size_t pixel_index = index * 4U;
    const uint8_t red = logo.pixels[pixel_index + 0];
    const uint8_t green = logo.pixels[pixel_index + 1];
    const uint8_t blue = logo.pixels[pixel_index + 2];
    const uint8_t alpha = logo.pixels[pixel_index + 3];
    overlay.alpha_mask[index] = alpha;
    rgbToYuv(red, green, blue, overlay.luma_mask[index], overlay.chroma_u_mask[index], overlay.chroma_v_mask[index]);
  }
  return overlay;
}

}  // namespace

struct OverlayLayerRenderer::Impl {
  RenderJob source_job;
  RenderJob text_job;
  int video_width = 0;
  int video_height = 0;
  bool text_enabled = false;
  bool logo_enabled = false;
  DecodedRgbaImage logo_image;
};

OverlayLayerRenderer::OverlayLayerRenderer() : impl_(std::make_unique<Impl>()) {
}

OverlayLayerRenderer::~OverlayLayerRenderer() = default;

void OverlayLayerRenderer::initialize(const RenderJob& job, int video_width, int video_height) {
  impl_->source_job = job;
  impl_->video_width = video_width;
  impl_->video_height = video_height;
  impl_->text_enabled = !job.watermark_text.empty();
  impl_->logo_enabled = !job.logo_path.empty();

  impl_->text_job = job;
  impl_->text_job.subtitle_srt.clear();
  impl_->text_job.subtitle_text = job.watermark_text;
  impl_->text_job.subtitle_font_family = job.watermark_font_family;
  impl_->text_job.subtitle_font_path = job.watermark_font_path;
  impl_->text_job.subtitle_font_size = job.watermark_font_size;
  impl_->text_job.subtitle_text_color = job.watermark_text_color;
  impl_->text_job.subtitle_outline_color = job.watermark_outline_color;
  impl_->text_job.subtitle_back_color = job.watermark_back_color;
  impl_->text_job.subtitle_outline = job.watermark_outline;
  impl_->text_job.subtitle_shadow = job.watermark_shadow;
  impl_->text_job.subtitle_margin = std::max(job.watermark_margin / 2, 0);
  impl_->text_job.subtitle_bold = job.watermark_bold;
  impl_->text_job.subtitle_italic = job.watermark_italic;
  impl_->text_job.subtitle_uppercase = job.watermark_uppercase;
  impl_->text_job.subtitle_opacity = job.watermark_opacity;
  impl_->text_job.subtitle_wrap = true;
  impl_->text_job.subtitle_clip = true;
  impl_->text_job.subtitle_auto_fit = true;
  impl_->text_job.subtitle_padding_x = std::max(job.watermark_margin / 3, 6);
  impl_->text_job.subtitle_padding_y = std::max(job.watermark_margin / 4, 4);
  impl_->text_job.subtitle_align_h = "center";
  impl_->text_job.subtitle_align_v = "middle";

  if (impl_->text_enabled) {
    text_renderer_.initialize(impl_->text_job, video_width, video_height);
  }

  if (impl_->logo_enabled) {
    const DecodedRgbaImage decoded_logo = decodeImageToRgba(job.logo_path);
    const int target_width = std::clamp(
        static_cast<int>(std::llround(static_cast<double>(video_width) * static_cast<double>(job.logo_scale))),
        1,
        video_width);
    const double aspect = decoded_logo.width > 0 ? static_cast<double>(decoded_logo.height) / decoded_logo.width : 1.0;
    const int target_height = std::clamp(static_cast<int>(std::llround(target_width * aspect)), 1, video_height);
    impl_->logo_image = scaleImageNearest(decoded_logo, target_width, target_height);
  }
}

std::vector<SubtitleOverlay> OverlayLayerRenderer::render(double timestamp_seconds) const {
  std::vector<SubtitleOverlay> overlays;

  if (impl_->logo_enabled && impl_->logo_image.width > 0 && impl_->logo_image.height > 0) {
    const Region logo_region = makeMotionRegion(
        impl_->video_width,
        impl_->video_height,
        impl_->logo_image.width,
        impl_->logo_image.height,
        impl_->source_job.logo_margin,
        impl_->source_job.logo_bounce,
        impl_->source_job.logo_speed_x,
        impl_->source_job.logo_speed_y,
        timestamp_seconds,
        17,
        29);
    overlays.push_back(logoToOverlay(impl_->logo_image, logo_region, impl_->source_job.logo_opacity));
  }

  if (impl_->text_enabled) {
    const int max_text_width = std::max(impl_->video_width - impl_->source_job.watermark_margin * 2, 1);
    const int min_text_width = std::min(220, max_text_width);
    const int text_box_width = std::clamp(impl_->video_width / 3, min_text_width, max_text_width);
    const int watermark_font_pixels = impl_->source_job.resolveWatermarkFontPixels(impl_->video_height);
    const int text_box_height =
        std::clamp(watermark_font_pixels * 3, watermark_font_pixels + 24, impl_->video_height / 4);
    const Region text_region = makeMotionRegion(
        impl_->video_width,
        impl_->video_height,
        text_box_width,
        text_box_height,
        impl_->source_job.watermark_margin,
        impl_->source_job.watermark_bounce,
        impl_->source_job.watermark_speed_x,
        impl_->source_job.watermark_speed_y,
        timestamp_seconds,
        73,
        41);
    SubtitleOverlay text_overlay;
    if (text_renderer_.available()) {
      text_overlay = text_renderer_.render(timestamp_seconds, &text_region);
    } else {
      text_overlay = SubtitleRenderer::buildOverlay(
          text_region,
          impl_->source_job.watermark_text,
          impl_->video_width,
          impl_->video_height,
          std::max(1, watermark_font_pixels / 10),
          std::max(impl_->source_job.watermark_margin / 2, 0),
          impl_->source_job.watermark_opacity);
    }
    if (text_overlay.enabled) {
      const auto [actual_x, actual_y] = computeMotionPosition(
          impl_->video_width,
          impl_->video_height,
          text_overlay.width,
          text_overlay.height,
          impl_->source_job.watermark_margin,
          impl_->source_job.watermark_bounce,
          impl_->source_job.watermark_speed_x,
          impl_->source_job.watermark_speed_y,
          timestamp_seconds,
          73,
          41);
      text_overlay.x = actual_x;
      text_overlay.y = actual_y;
      text_overlay.opacity = impl_->source_job.watermark_opacity;
      overlays.push_back(std::move(text_overlay));
    }
  }

  return overlays;
}

bool OverlayLayerRenderer::available() const {
  return impl_->logo_enabled || impl_->text_enabled;
}

}  // namespace video_engine
