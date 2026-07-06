#pragma once

#include <cuda_runtime.h>

extern "C" {
#include <libavutil/frame.h>
}

#include "cuda/CudaSubtitleRectEffect.h"

namespace video_engine {

class CudaOverlayCompositeEffect {
public:
  void apply(AVFrame* frame, const DeviceSubtitleOverlay& overlay, cudaStream_t stream) const;
};

}  // namespace video_engine
