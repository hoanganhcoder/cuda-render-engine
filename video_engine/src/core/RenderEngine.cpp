#include "core/RenderEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>

#include <libavutil/hwcontext.h>

#include "core/Logger.h"
#include "core/timeline/RenderJobAdapter.h"
#include "cuda/CudaSubtitleRectEffect.h"

namespace video_engine {

namespace {

struct RenderStageTimers {
  double overlay_seconds = 0.0;
  double upload_seconds = 0.0;
  double effect_seconds = 0.0;
  double encode_seconds = 0.0;

  void reset() {
    overlay_seconds = 0.0;
    upload_seconds = 0.0;
    effect_seconds = 0.0;
    encode_seconds = 0.0;
  }
};

double elapsedSecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - start).count();
}

int makeEven(int value) {
  return std::max(2, value - (value % 2));
}

double parseAspectRatio(const std::string& value) {
  const size_t colon = value.find(':');
  if (colon == std::string::npos) {
    return 16.0 / 9.0;
  }
  const double width = std::stod(value.substr(0, colon));
  const double height = std::stod(value.substr(colon + 1));
  return height > 0.0 ? width / height : 16.0 / 9.0;
}

void rgbToYuv(uint8_t red, uint8_t green, uint8_t blue, uint8_t& y, uint8_t& u, uint8_t& v) {
  const float yf = 0.299f * red + 0.587f * green + 0.114f * blue;
  const float uf = -0.168736f * red - 0.331264f * green + 0.5f * blue + 128.0f;
  const float vf = 0.5f * red - 0.418688f * green - 0.081312f * blue + 128.0f;
  y = static_cast<uint8_t>(std::clamp(yf, 0.0f, 255.0f));
  u = static_cast<uint8_t>(std::clamp(uf, 0.0f, 255.0f));
  v = static_cast<uint8_t>(std::clamp(vf, 0.0f, 255.0f));
}

uint8_t parseHexByte(const std::string& value, size_t offset) {
  return static_cast<uint8_t>(std::stoul(value.substr(offset, 2), nullptr, 16));
}

DeviceVideoTransform makeVideoTransform(
    const RenderJob& job,
    int input_width,
    int input_height,
    int output_width,
    int output_height) {
  DeviceVideoTransform transform;
  if (job.bg_color.size() >= 7 && job.bg_color[0] == '#') {
    rgbToYuv(
        parseHexByte(job.bg_color, 1),
        parseHexByte(job.bg_color, 3),
        parseHexByte(job.bg_color, 5),
        transform.bg_y,
        transform.bg_u,
        transform.bg_v);
  }

  const float scale_x = static_cast<float>(output_width) / static_cast<float>(input_width);
  const float scale_y = static_cast<float>(output_height) / static_cast<float>(input_height);
  float display_scale_x = scale_x;
  float display_scale_y = scale_y;
  if (job.resize_mode == "fit") {
    display_scale_x = display_scale_y = std::min(scale_x, scale_y);
  } else if (job.resize_mode == "fill") {
    display_scale_x = display_scale_y = std::max(scale_x, scale_y);
  }
  display_scale_x *= std::max(job.video_scale, 1.0f);
  display_scale_y *= std::max(job.video_scale, 1.0f);

  transform.display_width = static_cast<float>(input_width) * display_scale_x;
  transform.display_height = static_cast<float>(input_height) * display_scale_y;

  auto aligned_offset = [](const std::string& align, float output_size, float display_size) {
    if (align == "left" || align == "top") {
      return 0.0f;
    }
    if (align == "right" || align == "bottom") {
      return output_size - display_size;
    }
    return (output_size - display_size) * 0.5f;
  };
  transform.display_x = aligned_offset(job.video_align_h, static_cast<float>(output_width), transform.display_width);
  transform.display_y = aligned_offset(job.video_align_v, static_cast<float>(output_height), transform.display_height);
  return transform;
}

SubtitleOverlay makeCanvasOverlay(int x, int y, int width, int height) {
  SubtitleOverlay overlay;
  overlay.enabled = width > 0 && height > 0;
  overlay.x = x;
  overlay.y = y;
  overlay.width = width;
  overlay.height = height;
  overlay.stride = width;
  overlay.opacity = 1.0f;
  const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
  overlay.alpha_mask.assign(pixel_count, 0);
  overlay.luma_mask.assign(pixel_count, 0);
  overlay.chroma_u_mask.assign(pixel_count, 128);
  overlay.chroma_v_mask.assign(pixel_count, 128);
  return overlay;
}

void blendOverlayPixel(
    SubtitleOverlay& destination,
    size_t dst_index,
    float src_alpha,
    uint8_t src_y,
    uint8_t src_u,
    uint8_t src_v) {
  if (src_alpha <= 0.0f) {
    return;
  }

  const float dst_alpha = static_cast<float>(destination.alpha_mask[dst_index]) / 255.0f;
  const float out_alpha = src_alpha + dst_alpha * (1.0f - src_alpha);
  if (out_alpha <= 0.0f) {
    return;
  }

  const float dst_y = static_cast<float>(destination.luma_mask[dst_index]) / 255.0f;
  const float dst_u = static_cast<float>(destination.chroma_u_mask[dst_index]) / 255.0f;
  const float dst_v = static_cast<float>(destination.chroma_v_mask[dst_index]) / 255.0f;
  const float src_yf = static_cast<float>(src_y) / 255.0f;
  const float src_uf = static_cast<float>(src_u) / 255.0f;
  const float src_vf = static_cast<float>(src_v) / 255.0f;
  const float dst_weight = dst_alpha * (1.0f - src_alpha);

  const auto mix_channel = [&](float src_channel, float dst_channel) {
    return (src_channel * src_alpha + dst_channel * dst_weight) / out_alpha;
  };

  destination.alpha_mask[dst_index] = static_cast<uint8_t>(std::clamp(out_alpha * 255.0f, 0.0f, 255.0f));
  destination.luma_mask[dst_index] = static_cast<uint8_t>(std::clamp(mix_channel(src_yf, dst_y) * 255.0f, 0.0f, 255.0f));
  destination.chroma_u_mask[dst_index] =
      static_cast<uint8_t>(std::clamp(mix_channel(src_uf, dst_u) * 255.0f, 0.0f, 255.0f));
  destination.chroma_v_mask[dst_index] =
      static_cast<uint8_t>(std::clamp(mix_channel(src_vf, dst_v) * 255.0f, 0.0f, 255.0f));
}

void blitOverlay(SubtitleOverlay& destination, const SubtitleOverlay& source) {
  if (!source.enabled) {
    return;
  }

  const float opacity = std::clamp(source.opacity, 0.0f, 1.0f);
  for (int y = 0; y < source.height; ++y) {
    const int dst_y = source.y + y;
    if (dst_y < destination.y || dst_y >= destination.y + destination.height) {
      continue;
    }
    for (int x = 0; x < source.width; ++x) {
      const int dst_x = source.x + x;
      if (dst_x < destination.x || dst_x >= destination.x + destination.width) {
        continue;
      }

      const size_t src_index = static_cast<size_t>(y) * static_cast<size_t>(source.stride) + static_cast<size_t>(x);
      const float src_alpha = (static_cast<float>(source.alpha_mask[src_index]) / 255.0f) * opacity;
      if (src_alpha <= 0.0f) {
        continue;
      }

      const int local_dst_x = dst_x - destination.x;
      const int local_dst_y = dst_y - destination.y;
      if (local_dst_x < 0 || local_dst_y < 0 || local_dst_x >= destination.width ||
          local_dst_y >= destination.height) {
        continue;
      }
      const size_t dst_index =
          static_cast<size_t>(local_dst_y) * static_cast<size_t>(destination.stride) + static_cast<size_t>(local_dst_x);
      blendOverlayPixel(
          destination,
          dst_index,
          src_alpha,
          source.luma_mask[src_index],
          source.chroma_u_mask[src_index],
          source.chroma_v_mask[src_index]);
    }
  }
}

SubtitleOverlay combineOverlays(const std::vector<SubtitleOverlay>& overlays, int width, int height) {
  std::vector<const SubtitleOverlay*> enabled_overlays;
  enabled_overlays.reserve(overlays.size());
  for (const SubtitleOverlay& overlay : overlays) {
    if (overlay.enabled) {
      enabled_overlays.push_back(&overlay);
    }
  }

  if (enabled_overlays.empty()) {
    return SubtitleOverlay{};
  }
  if (enabled_overlays.size() == 1U) {
    return *enabled_overlays.front();
  }

  int min_x = width;
  int min_y = height;
  int max_x = 0;
  int max_y = 0;
  for (const SubtitleOverlay* overlay : enabled_overlays) {
    min_x = std::min(min_x, std::clamp(overlay->x, 0, width));
    min_y = std::min(min_y, std::clamp(overlay->y, 0, height));
    max_x = std::max(max_x, std::clamp(overlay->x + overlay->width, 0, width));
    max_y = std::max(max_y, std::clamp(overlay->y + overlay->height, 0, height));
  }
  if (max_x <= min_x || max_y <= min_y) {
    return SubtitleOverlay{};
  }

  SubtitleOverlay canvas = makeCanvasOverlay(min_x, min_y, max_x - min_x, max_y - min_y);
  for (const SubtitleOverlay* overlay : enabled_overlays) {
    blitOverlay(canvas, *overlay);
  }
  return canvas;
}

bool needsPreviousFrameHistory(const std::vector<Region>& regions) {
  for (const Region& region : regions) {
    if (region.temporal_blend > 0.0f) {
      return true;
    }
  }
  return false;
}

bool sameUploadedOverlay(const SubtitleOverlay& previous, const SubtitleOverlay& current) {
  return previous.enabled == current.enabled &&
         previous.width == current.width && previous.height == current.height && previous.stride == current.stride &&
         previous.opacity == current.opacity && previous.cue_text == current.cue_text &&
         previous.alpha_mask.size() == current.alpha_mask.size() && previous.luma_mask.size() == current.luma_mask.size() &&
         previous.chroma_u_mask.size() == current.chroma_u_mask.size() &&
         previous.chroma_v_mask.size() == current.chroma_v_mask.size();
}

}  // namespace

RenderEngine::RenderEngine() {
  cuda_context_.initialize();
}

bool RenderEngine::render(const RenderJob& input_job) {
  AVFrame* output_frame = nullptr;
  AVFrame* previous_frame = nullptr;
  try {
    RenderJob job = input_job;
    const timeline::Sequence sequence_input = timeline::RenderJobAdapter::toSequence(job);
    FFmpegDecoder decoder;
    FFmpegEncoder encoder;
    AVFrame* decoded_frame = nullptr;
    double timestamp_seconds = 0.0;
    SubtitleOverlay current_subtitle_overlay;
    SubtitleOverlay uploaded_subtitle_overlay;
    bool has_uploaded_subtitle_overlay = false;
    SubtitleOverlay current_top_overlay;
    SubtitleOverlay uploaded_top_overlay;
    bool has_uploaded_top_overlay = false;

    decoder.open(job.input);
    if (job.width <= 0 && job.height <= 0) {
      job.height = decoder.height();
      job.width = makeEven(static_cast<int>(std::lround(static_cast<double>(job.height) * parseAspectRatio(job.video_aspect_ratio))));
    } else if (job.width <= 0) {
      job.width = makeEven(static_cast<int>(std::lround(static_cast<double>(job.height) * parseAspectRatio(job.video_aspect_ratio))));
    } else if (job.height <= 0) {
      job.height = makeEven(static_cast<int>(std::lround(static_cast<double>(job.width) / parseAspectRatio(job.video_aspect_ratio))));
    }
    if (job.fps <= 0.0) {
      job.fps = decoder.fps();
    }
    if (job.width <= 0 || job.height <= 0 || job.fps <= 0.0) {
      throw std::runtime_error("Unable to determine output width/height/fps from job or decoder.");
    }

    for (Region& region : job.regions) {
      region.clampToBounds(job.width, job.height);
    }
    for (Region& region : job.subtitle_regions) {
      region.clampToBounds(job.width, job.height);
    }
    for (Region& region : job.blur_regions) {
      region.clampToBounds(job.width, job.height);
    }
    const std::vector<Region>& effective_blur_regions = job.blur_regions.empty() ? job.regions : job.blur_regions;
    const bool use_previous_frame_history = needsPreviousFrameHistory(effective_blur_regions);
    blur_box_effect_.initialize(sequence_input);
    current_job_ = job;
    current_video_width_ = job.width;
    current_video_height_ = job.height;
    subtitle_layer_renderer_.initialize(job, job.width, job.height);
    overlay_layer_renderer_.initialize(job, job.width, job.height);
    const DeviceVideoTransform video_transform =
        makeVideoTransform(job, decoder.width(), decoder.height(), job.width, job.height);
    if (!job.subtitle_srt.empty() || !job.subtitle_text.empty()) {
      if (subtitle_layer_renderer_.available()) {
        Logger::info("Using subtitle layer renderer.");
      } else {
        Logger::warn("Subtitle layer renderer unavailable.");
      }
    }

    Logger::info("Render started.");
    const auto render_start_time = std::chrono::steady_clock::now();
    auto previous_log_time = render_start_time;
    RenderStageTimers interval_timers;
    int frame_index = 0;
    while (decoder.read(decoded_frame, timestamp_seconds)) {
      if (!decoded_frame->hw_frames_ctx) {
        throw std::runtime_error("Decoded frame does not carry CUDA hardware frames context.");
      }
      if (decoder.softwarePixelFormat() != AV_PIX_FMT_NV12) {
        throw std::runtime_error("Zero-copy path currently expects NVDEC to output NV12 surfaces.");
      }

      if (!output_frame) {
        encoder.open(job.output, job.width, job.height, job.fps, decoder.hwDeviceContext(), decoded_frame->hw_frames_ctx);
        output_frame = allocateHardwareFrame(encoder.hwFramesContext(), job.width, job.height);
        if (use_previous_frame_history) {
          previous_frame = allocateHardwareFrame(encoder.hwFramesContext(), job.width, job.height);
        }
      }

      const std::vector<Region> active_blur_regions = blur_box_effect_.collectActiveRegions(timestamp_seconds);
      const auto overlay_start_time = std::chrono::steady_clock::now();
      current_subtitle_overlay = buildSubtitleOverlay(timestamp_seconds);
      current_top_overlay = buildTopOverlay(timestamp_seconds);
      interval_timers.overlay_seconds += elapsedSecondsSince(overlay_start_time);
      const auto upload_start_time = std::chrono::steady_clock::now();
      DeviceSubtitleOverlay subtitle_device_overlay = uploadOverlay(
          current_subtitle_overlay,
          uploaded_subtitle_overlay,
          has_uploaded_subtitle_overlay,
          subtitle_mask_buffer_,
          subtitle_luma_buffer_,
          subtitle_chroma_u_buffer_,
          subtitle_chroma_v_buffer_);
      DeviceSubtitleOverlay top_device_overlay = uploadOverlay(
          current_top_overlay,
          uploaded_top_overlay,
          has_uploaded_top_overlay,
          overlay_mask_buffer_,
          overlay_luma_buffer_,
          overlay_chroma_u_buffer_,
          overlay_chroma_v_buffer_);
      interval_timers.upload_seconds += elapsedSecondsSince(upload_start_time);

      const auto effect_start_time = std::chrono::steady_clock::now();
      blur_box_effect_.apply(
          decoded_frame,
          use_previous_frame_history && frame_index > 0 ? previous_frame : nullptr,
          output_frame,
          active_blur_regions,
          1.0f,
          job.flip_horizontal,
          video_transform,
          DeviceSubtitleOverlay{},
          cuda_context_.stream());
      static const std::vector<Region> kNoBlurRegions;
      const DeviceVideoTransform identity_transform{
          0.0f,
          0.0f,
          static_cast<float>(job.width),
          static_cast<float>(job.height),
          16,
          128,
          128};
      if (subtitle_device_overlay.enabled()) {
        blur_box_effect_.apply(
            output_frame,
            nullptr,
            output_frame,
            kNoBlurRegions,
            1.0f,
            false,
            identity_transform,
            subtitle_device_overlay,
            cuda_context_.stream());
      }
      if (top_device_overlay.enabled()) {
        blur_box_effect_.apply(
            output_frame,
            nullptr,
            output_frame,
            kNoBlurRegions,
            1.0f,
            false,
            identity_transform,
            top_device_overlay,
            cuda_context_.stream());
      }
      interval_timers.effect_seconds += elapsedSecondsSince(effect_start_time);
      output_frame->format = AV_PIX_FMT_CUDA;
      output_frame->width = job.width;
      output_frame->height = job.height;
      const auto encode_start_time = std::chrono::steady_clock::now();
      encoder.write(output_frame);
      interval_timers.encode_seconds += elapsedSecondsSince(encode_start_time);
      if (use_previous_frame_history) {
        cloneFrameTo(previous_frame, output_frame);
      }

      ++frame_index;
      if (frame_index % kLogFrameInterval == 0) {
        const auto now = std::chrono::steady_clock::now();
        const double total_elapsed =
            std::chrono::duration_cast<std::chrono::duration<double>>(now - render_start_time).count();
        const double interval_elapsed =
            std::chrono::duration_cast<std::chrono::duration<double>>(now - previous_log_time).count();
        previous_log_time = now;
        std::ostringstream stream;
        stream << "Rendered " << frame_index << " GPU frames at t=" << timestamp_seconds << "s"
               << " interval_fps=" << (interval_elapsed > 0.0 ? static_cast<double>(kLogFrameInterval) / interval_elapsed : 0.0)
               << " avg_fps=" << (total_elapsed > 0.0 ? static_cast<double>(frame_index) / total_elapsed : 0.0)
               << " stage_ms={text:" << (interval_timers.overlay_seconds * 1000.0)
               << ",upload:" << (interval_timers.upload_seconds * 1000.0)
               << ",effect:" << (interval_timers.effect_seconds * 1000.0)
               << ",encode:" << (interval_timers.encode_seconds * 1000.0) << "}";
        Logger::info(stream.str());
        interval_timers.reset();
      }
    }

    av_frame_free(&output_frame);
    av_frame_free(&previous_frame);
    encoder.close();
    decoder.close();
    Logger::info("Render completed successfully.");
    return true;
  } catch (const std::exception& error) {
    av_frame_free(&output_frame);
    av_frame_free(&previous_frame);
    Logger::error(error.what());
    return false;
  }
}

SubtitleOverlay RenderEngine::buildSubtitleOverlay(double timestamp_seconds) const {
  if (subtitle_layer_renderer_.available()) {
    const std::vector<SubtitleOverlay> subtitle_layers = subtitle_layer_renderer_.render(timestamp_seconds);
    return combineOverlays(subtitle_layers, current_video_width_, current_video_height_);
  }
  return SubtitleOverlay{};
}

SubtitleOverlay RenderEngine::buildTopOverlay(double timestamp_seconds) const {
  if (overlay_layer_renderer_.available()) {
    const std::vector<SubtitleOverlay> overlay_layers = overlay_layer_renderer_.render(timestamp_seconds);
    return combineOverlays(overlay_layers, current_video_width_, current_video_height_);
  }
  return SubtitleOverlay{};
}

DeviceSubtitleOverlay RenderEngine::uploadOverlay(
    const SubtitleOverlay& overlay,
    SubtitleOverlay& uploaded_overlay,
    bool& has_uploaded_overlay,
    CudaBuffer& alpha_buffer,
    CudaBuffer& luma_buffer,
    CudaBuffer& chroma_u_buffer,
    CudaBuffer& chroma_v_buffer) {
  if (!has_uploaded_overlay || !sameUploadedOverlay(uploaded_overlay, overlay)) {
    if (overlay.enabled) {
      alpha_buffer.upload(overlay.alpha_mask, cuda_context_.stream());
      luma_buffer.upload(overlay.luma_mask, cuda_context_.stream());
      chroma_u_buffer.upload(overlay.chroma_u_mask, cuda_context_.stream());
      chroma_v_buffer.upload(overlay.chroma_v_mask, cuda_context_.stream());
    } else {
      alpha_buffer.release();
      luma_buffer.release();
      chroma_u_buffer.release();
      chroma_v_buffer.release();
    }
    uploaded_overlay = overlay;
    has_uploaded_overlay = true;
  }

  DeviceSubtitleOverlay device_overlay{};
  if (overlay.enabled && alpha_buffer.allocated() && luma_buffer.allocated() && chroma_u_buffer.allocated() &&
      chroma_v_buffer.allocated()) {
    device_overlay.alpha_mask = alpha_buffer.data();
    device_overlay.luma_mask = luma_buffer.data();
    device_overlay.chroma_u_mask = chroma_u_buffer.data();
    device_overlay.chroma_v_mask = chroma_v_buffer.data();
    device_overlay.x = overlay.x;
    device_overlay.y = overlay.y;
    device_overlay.width = overlay.width;
    device_overlay.height = overlay.height;
    device_overlay.stride = overlay.stride;
    device_overlay.opacity = overlay.opacity;
  }
  return device_overlay;
}

AVFrame* RenderEngine::allocateHardwareFrame(AVBufferRef* hw_frames_context, int width, int height) const {
  AVFrame* frame = av_frame_alloc();
  if (!frame) {
    throw std::runtime_error("Failed to allocate hardware AVFrame.");
  }
  frame->format = AV_PIX_FMT_CUDA;
  frame->width = width;
  frame->height = height;
  frame->hw_frames_ctx = av_buffer_ref(hw_frames_context);
  if (!frame->hw_frames_ctx) {
    av_frame_free(&frame);
    throw std::runtime_error("Failed to retain hw_frames_ctx for scratch frame.");
  }
  const int result = av_hwframe_get_buffer(frame->hw_frames_ctx, frame, 0);
  if (result < 0) {
    av_frame_free(&frame);
    throw std::runtime_error("Failed to allocate scratch CUDA frame from FFmpeg hw_frames_ctx.");
  }
  return frame;
}

void RenderEngine::cloneFrameTo(AVFrame* destination, const AVFrame* source) const {
  const int result = av_frame_copy(destination, source);
  if (result < 0) {
    throw std::runtime_error("Failed to copy GPU frame into previous-frame history buffer.");
  }
  destination->pts = source->pts;
}

}  // namespace video_engine
