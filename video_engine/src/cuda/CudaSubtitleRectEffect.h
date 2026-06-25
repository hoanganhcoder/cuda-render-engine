#pragma once

#include <vector>

#include <cuda_runtime.h>

extern "C" {
#include <libavutil/frame.h>
}

#include "core/Region.h"
#include "cuda/CudaBuffer.h"

namespace video_engine {

struct DeviceSubtitleOverlay {
  const uint8_t* alpha_mask = nullptr;
  const uint8_t* luma_mask = nullptr;
  const uint8_t* chroma_u_mask = nullptr;
  const uint8_t* chroma_v_mask = nullptr;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int stride = 0;
  float opacity = 1.0f;

  __host__ __device__ [[nodiscard]] bool enabled() const {
    return alpha_mask != nullptr && luma_mask != nullptr && chroma_u_mask != nullptr && chroma_v_mask != nullptr &&
           width > 0 && height > 0;
  }
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
      bool gaussian_blur,
      const DeviceSubtitleOverlay& text_overlay,
      cudaStream_t stream) const;

private:
  mutable CudaBuffer temp_luma_;
  mutable CudaBuffer temp_chroma_;
};

}  // namespace video_engine
