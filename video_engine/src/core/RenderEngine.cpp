#include "core/RenderEngine.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include <libavutil/hwcontext.h>

#include "core/Logger.h"
#include "core/SubtitleRenderer.h"

namespace video_engine {

RenderEngine::RenderEngine() {
  cuda_context_.initialize();
}

bool RenderEngine::render(const RenderJob& input_job) {
  AVFrame* output_frame = nullptr;
  AVFrame* previous_frame = nullptr;
  try {
    RenderJob job = input_job;
    FFmpegDecoder decoder;
    FFmpegEncoder encoder;
    AVFrame* decoded_frame = nullptr;
    double timestamp_seconds = 0.0;
    SubtitleOverlay current_overlay;

    decoder.open(job.input);
    if (job.width <= 0) {
      job.width = decoder.width();
    }
    if (job.height <= 0) {
      job.height = decoder.height();
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
    ass_subtitle_renderer_.initialize(job, job.width, job.height);

    Logger::info("Render started.");
    int frame_index = 0;
    while (decoder.read(decoded_frame, timestamp_seconds)) {
      if (decoded_frame->width != job.width || decoded_frame->height != job.height) {
        throw std::runtime_error("Decoder output resolution differs from configured output resolution.");
      }
      if (!decoded_frame->hw_frames_ctx) {
        throw std::runtime_error("Decoded frame does not carry CUDA hardware frames context.");
      }
      if (decoder.softwarePixelFormat() != AV_PIX_FMT_NV12) {
        throw std::runtime_error("Zero-copy path currently expects NVDEC to output NV12 surfaces.");
      }

      if (!output_frame) {
        output_frame = allocateHardwareFrame(decoded_frame->hw_frames_ctx, job.width, job.height);
        previous_frame = allocateHardwareFrame(decoded_frame->hw_frames_ctx, job.width, job.height);
        encoder.open(job.output, job.width, job.height, job.fps, decoder.hwDeviceContext(), decoded_frame->hw_frames_ctx);
      }

      const std::vector<Region> active_regions = collectActiveRegions(job, timestamp_seconds);
      current_overlay = buildSubtitleOverlay(job, active_regions, timestamp_seconds);
      if (current_overlay.enabled) {
        subtitle_mask_buffer_.upload(current_overlay.alpha_mask, cuda_context_.stream());
        subtitle_luma_buffer_.upload(current_overlay.luma_mask, cuda_context_.stream());
        subtitle_chroma_u_buffer_.upload(current_overlay.chroma_u_mask, cuda_context_.stream());
        subtitle_chroma_v_buffer_.upload(current_overlay.chroma_v_mask, cuda_context_.stream());
      } else {
        subtitle_mask_buffer_.release();
        subtitle_luma_buffer_.release();
        subtitle_chroma_u_buffer_.release();
        subtitle_chroma_v_buffer_.release();
      }

      DeviceSubtitleOverlay device_overlay{};
      if (current_overlay.enabled && subtitle_mask_buffer_.allocated() && subtitle_luma_buffer_.allocated() &&
          subtitle_chroma_u_buffer_.allocated() && subtitle_chroma_v_buffer_.allocated()) {
        device_overlay.alpha_mask = subtitle_mask_buffer_.data();
        device_overlay.luma_mask = subtitle_luma_buffer_.data();
        device_overlay.chroma_u_mask = subtitle_chroma_u_buffer_.data();
        device_overlay.chroma_v_mask = subtitle_chroma_v_buffer_.data();
        device_overlay.x = current_overlay.x;
        device_overlay.y = current_overlay.y;
        device_overlay.width = current_overlay.width;
        device_overlay.height = current_overlay.height;
        device_overlay.stride = current_overlay.stride;
        device_overlay.opacity = current_overlay.opacity;
      }

      subtitle_effect_.apply(
          decoded_frame,
          frame_index > 0 ? previous_frame : nullptr,
          output_frame,
          active_regions,
          device_overlay,
          cuda_context_.stream());
      output_frame->format = AV_PIX_FMT_CUDA;
      output_frame->width = job.width;
      output_frame->height = job.height;
      encoder.write(output_frame);
      cloneFrameTo(previous_frame, output_frame);

      ++frame_index;
      if (frame_index % kLogFrameInterval == 0) {
        std::ostringstream stream;
        stream << "Rendered " << frame_index << " GPU frames at t=" << timestamp_seconds << "s";
        Logger::info(stream.str());
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

std::vector<Region> RenderEngine::collectActiveRegions(const RenderJob& job, double timestamp) const {
  std::vector<Region> active_regions;
  active_regions.reserve(std::min(static_cast<int>(job.regions.size()), CudaSubtitleRectEffect::kMaxRegions));
  for (const Region& region : job.regions) {
    if (region.isActive(timestamp) && region.w > 0 && region.h > 0) {
      active_regions.push_back(region);
      if (static_cast<int>(active_regions.size()) == CudaSubtitleRectEffect::kMaxRegions) {
        break;
      }
    }
  }
  return active_regions;
}

SubtitleOverlay RenderEngine::buildSubtitleOverlay(
    const RenderJob& job,
    const std::vector<Region>& active_regions,
    double timestamp_seconds) const {
  if (active_regions.empty()) {
    return SubtitleOverlay{};
  }
  if (ass_subtitle_renderer_.available()) {
    return ass_subtitle_renderer_.render(timestamp_seconds, &active_regions.front());
  }

  if (!job.subtitle_text.empty()) {
    return SubtitleRenderer::buildOverlay(
        active_regions.front(),
        job.subtitle_text,
        job.width,
        job.height,
        job.subtitle_font_scale,
        job.subtitle_margin,
        job.subtitle_opacity);
  }
  return SubtitleOverlay{};
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
