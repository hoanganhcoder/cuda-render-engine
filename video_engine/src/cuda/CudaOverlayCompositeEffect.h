#pragma once

#include <cstdint>

#include <cuda_runtime.h>

extern "C" {
#include <libavutil/frame.h>
}

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

class CudaOverlayCompositeEffect {
public:
  void apply(AVFrame* frame, const DeviceSubtitleOverlay& overlay, cudaStream_t stream) const;
};

}  // namespace video_engine
