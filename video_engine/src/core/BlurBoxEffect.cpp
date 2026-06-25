#include "core/BlurBoxEffect.h"

#include <algorithm>

namespace video_engine {

void BlurBoxEffect::initialize(const timeline::Sequence& sequence) {
  sequence_ = sequence;
}

std::vector<Region> BlurBoxEffect::collectActiveRegions(double timestamp) const {
  std::vector<Region> active_regions;
  active_regions.reserve(std::min(static_cast<int>(sequence_.blur_box.regions.size()), CudaSubtitleRectEffect::kMaxRegions));
  for (const Region& region : sequence_.blur_box.regions) {
    if (region.isActive(timestamp) && region.w > 0 && region.h > 0) {
      active_regions.push_back(region);
      if (static_cast<int>(active_regions.size()) == CudaSubtitleRectEffect::kMaxRegions) {
        break;
      }
    }
  }
  return active_regions;
}

void BlurBoxEffect::apply(
    const AVFrame* source_frame,
    const AVFrame* previous_frame,
    AVFrame* output_frame,
    const std::vector<Region>& active_regions,
    float video_scale,
    bool flip_horizontal,
    const DeviceSubtitleOverlay& text_overlay,
    cudaStream_t stream) const {
  cuda_effect_.apply(
      source_frame,
      previous_frame,
      output_frame,
      active_regions,
      video_scale,
      flip_horizontal,
      sequence_.blur_box.enabled,
      text_overlay,
      stream);
}

}  // namespace video_engine
