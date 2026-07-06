#pragma once

#include <cstdint>
#include <vector>

#include <cuda_runtime.h>

extern "C" {
#include <libavutil/frame.h>
}

#include "core/Region.h"
#include "cuda/CudaBuffer.h"

namespace video_engine {

struct DeviceVideoTransform {
  float display_x = 0.0f;
  float display_y = 0.0f;
  float display_width = 0.0f;
  float display_height = 0.0f;
  uint8_t bg_y = 16;
  uint8_t bg_u = 128;
  uint8_t bg_v = 128;
};

class CudaSubtitleRectEffect {
public:
  static constexpr int kMaxRegions = 64;

  void apply(
      const AVFrame* source_frame,
      const AVFrame* previous_frame,
      AVFrame* output_frame,
      const std::vector<Region>& active_regions,
      float video_scale,
      bool flip_horizontal,
      const DeviceVideoTransform& transform,
      bool gaussian_blur,
      cudaStream_t stream) const;

private:
  mutable CudaBuffer temp_luma_;
  mutable CudaBuffer temp_luma_blur_;
  mutable CudaBuffer temp_chroma_;
  mutable CudaBuffer temp_chroma_blur_;
};

}  // namespace video_engine
