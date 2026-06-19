#pragma once

#include <vector>

extern "C" {
#include <libavutil/frame.h>
}

#include "codec/FFmpegDecoder.h"
#include "codec/FFmpegEncoder.h"
#include "core/RenderJob.h"
#include "cuda/CudaContext.h"
#include "cuda/CudaSubtitleRectEffect.h"

namespace video_engine {

class RenderEngine {
public:
  RenderEngine();
  bool render(const RenderJob& job);

private:
  static constexpr int kLogFrameInterval = 100;

  std::vector<Region> collectActiveRegions(const RenderJob& job, double timestamp) const;
  AVFrame* allocateHardwareFrame(AVBufferRef* hw_frames_context, int width, int height) const;
  void cloneFrameTo(AVFrame* destination, const AVFrame* source) const;

  CudaContext cuda_context_;
  CudaSubtitleRectEffect subtitle_effect_;
};

}  // namespace video_engine
