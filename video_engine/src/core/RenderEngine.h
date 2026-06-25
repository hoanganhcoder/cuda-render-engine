#pragma once

#include <vector>

extern "C" {
#include <libavutil/frame.h>
}

#include "codec/FFmpegDecoder.h"
#include "codec/FFmpegEncoder.h"
#include "core/AssSubtitleRenderer.h"
#include "core/BlurBoxEffect.h"
#include "core/OverlayLayerRenderer.h"
#include "core/RenderJob.h"
#include "core/SubtitleOverlay.h"
#include "core/TextBoxRenderer.h"
#include "core/timeline/Sequence.h"
#include "cuda/CudaContext.h"
#include "cuda/CudaBuffer.h"
#include "cuda/CudaSubtitleRectEffect.h"

namespace video_engine {

class RenderEngine {
public:
  RenderEngine();
  bool render(const RenderJob& job);

private:
  static constexpr int kLogFrameInterval = 100;

  std::vector<Region> collectActiveRegions(const RenderJob& job, double timestamp) const;
  SubtitleOverlay buildSubtitleOverlay(
      const RenderJob& job,
      const std::vector<Region>& active_regions,
      double timestamp_seconds) const;
  AVFrame* allocateHardwareFrame(AVBufferRef* hw_frames_context, int width, int height) const;
  void cloneFrameTo(AVFrame* destination, const AVFrame* source) const;

  CudaContext cuda_context_;
  CudaBuffer subtitle_mask_buffer_;
  CudaBuffer subtitle_luma_buffer_;
  CudaBuffer subtitle_chroma_u_buffer_;
  CudaBuffer subtitle_chroma_v_buffer_;
  BlurBoxEffect blur_box_effect_;
  OverlayLayerRenderer overlay_layer_renderer_;
  TextBoxRenderer text_box_renderer_;
  AssSubtitleRenderer ass_subtitle_renderer_;
};

}  // namespace video_engine
