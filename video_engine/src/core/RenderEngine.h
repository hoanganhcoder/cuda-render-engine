#pragma once

#include <vector>

extern "C" {
#include <libavutil/frame.h>
}

#include "codec/FFmpegDecoder.h"
#include "codec/FFmpegEncoder.h"
#include "core/RenderJob.h"
#include "core/SubtitleCue.h"
#include "core/SubtitleOverlay.h"
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
  const SubtitleCue* findActiveCue(const std::vector<SubtitleCue>& cues, double timestamp) const;
  SubtitleOverlay buildSubtitleOverlay(
      const RenderJob& job,
      const std::vector<Region>& active_regions,
      const SubtitleCue* active_cue) const;
  AVFrame* allocateHardwareFrame(AVBufferRef* hw_frames_context, int width, int height) const;
  void cloneFrameTo(AVFrame* destination, const AVFrame* source) const;

  CudaContext cuda_context_;
  CudaBuffer subtitle_mask_buffer_;
  CudaSubtitleRectEffect subtitle_effect_;
};

}  // namespace video_engine
