#pragma once

#include <vector>

extern "C" {
#include <libavutil/frame.h>
}

#include "codec/FFmpegDecoder.h"
#include "codec/FFmpegEncoder.h"
#include "core/BlurBoxEffect.h"
#include "core/OverlayLayerRenderer.h"
#include "core/RenderJob.h"
#include "core/SubtitleLayerRenderer.h"
#include "core/SubtitleOverlay.h"
#include "cuda/CudaContext.h"
#include "cuda/CudaBuffer.h"
#include "cuda/CudaOverlayCompositeEffect.h"

namespace video_engine {

class RenderEngine {
public:
  RenderEngine();
  bool render(const RenderJob& job);

private:
  static constexpr int kLogFrameInterval = 100;

  SubtitleOverlay buildSubtitleOverlay(double timestamp_seconds) const;
  SubtitleOverlay buildTopOverlay(double timestamp_seconds) const;
  DeviceSubtitleOverlay uploadOverlay(
      const SubtitleOverlay& overlay,
      SubtitleOverlay& uploaded_overlay,
      bool& has_uploaded_overlay,
      CudaBuffer& alpha_buffer,
      CudaBuffer& luma_buffer,
      CudaBuffer& chroma_u_buffer,
      CudaBuffer& chroma_v_buffer);
  AVFrame* allocateHardwareFrame(AVBufferRef* hw_frames_context, int width, int height) const;
  void cloneFrameTo(AVFrame* destination, const AVFrame* source) const;

  RenderJob current_job_;
  int current_video_width_ = 0;
  int current_video_height_ = 0;
  CudaContext cuda_context_;
  CudaBuffer subtitle_mask_buffer_;
  CudaBuffer subtitle_luma_buffer_;
  CudaBuffer subtitle_chroma_u_buffer_;
  CudaBuffer subtitle_chroma_v_buffer_;
  CudaBuffer overlay_mask_buffer_;
  CudaBuffer overlay_luma_buffer_;
  CudaBuffer overlay_chroma_u_buffer_;
  CudaBuffer overlay_chroma_v_buffer_;
  BlurBoxEffect blur_box_effect_;
  CudaOverlayCompositeEffect overlay_composite_effect_;
  OverlayLayerRenderer overlay_layer_renderer_;
  SubtitleLayerRenderer subtitle_layer_renderer_;
};

}  // namespace video_engine
