#pragma once

#include <vector>

#include <cuda_runtime.h>

extern "C" {
#include <libavutil/frame.h>
}

#include "core/Region.h"

namespace video_engine {

struct DeviceSubtitleOverlay {
  const uint8_t* alpha_mask = nullptr;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int stride = 0;
  uint8_t luma = 235;
  uint8_t chroma_u = 128;
  uint8_t chroma_v = 128;
  float opacity = 1.0f;

  [[nodiscard]] bool enabled() const { return alpha_mask != nullptr && width > 0 && height > 0; }
};

class CudaSubtitleRectEffect {
public:
  static constexpr int kMaxRegions = 64;

  void apply(
      const AVFrame* source_frame,
      const AVFrame* previous_frame,
      AVFrame* output_frame,
      const std::vector<Region>& active_regions,
      const DeviceSubtitleOverlay& text_overlay,
      cudaStream_t stream) const;
};

}  // namespace video_engine
