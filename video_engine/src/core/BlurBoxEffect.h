#pragma once

#include <vector>

extern "C" {
#include <libavutil/frame.h>
}

#include "core/Region.h"
#include "core/timeline/Sequence.h"
#include "cuda/CudaSubtitleRectEffect.h"

namespace video_engine {

class BlurBoxEffect {
public:
  void initialize(const timeline::Sequence& sequence);
  [[nodiscard]] std::vector<Region> collectActiveRegions(double timestamp) const;
  void apply(
      const AVFrame* source_frame,
      const AVFrame* previous_frame,
      AVFrame* output_frame,
      const std::vector<Region>& active_regions,
      float video_scale,
      bool flip_horizontal,
      const DeviceSubtitleOverlay& text_overlay,
      cudaStream_t stream) const;

private:
  timeline::Sequence sequence_;
  CudaSubtitleRectEffect cuda_effect_;
};

}  // namespace video_engine
