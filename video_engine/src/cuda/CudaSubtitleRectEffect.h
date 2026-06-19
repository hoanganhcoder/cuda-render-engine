#pragma once

#include <vector>

#include <cuda_runtime.h>

extern "C" {
#include <libavutil/frame.h>
}

#include "core/Region.h"

namespace video_engine {

class CudaSubtitleRectEffect {
public:
  static constexpr int kMaxRegions = 64;

  void apply(
      const AVFrame* source_frame,
      const AVFrame* previous_frame,
      AVFrame* output_frame,
      const std::vector<Region>& active_regions,
      cudaStream_t stream) const;
};

}  // namespace video_engine
