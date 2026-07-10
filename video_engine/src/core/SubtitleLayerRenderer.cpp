#include "core/SubtitleLayerRenderer.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "cuda/CudaSubtitleRectEffect.h"

namespace video_engine {

namespace {

std::vector<Region> collectActiveRegions(const std::vector<Region>& regions, double timestamp) {
  std::vector<Region> active_regions;
  active_regions.reserve(std::min(static_cast<int>(regions.size()), CudaSubtitleRectEffect::kMaxRegions));
  for (const Region& region : regions) {
    if (region.isActive(timestamp) && region.w > 0 && region.h > 0) {
      active_regions.push_back(region);
      if (static_cast<int>(active_regions.size()) == CudaSubtitleRectEffect::kMaxRegions) {
        break;
      }
    }
  }
  return active_regions;
}

}  // namespace

struct SubtitleLayerRenderer::Impl {
  RenderJob job;
  int video_width = 0;
  int video_height = 0;
  bool use_ass = false;
};

SubtitleLayerRenderer::SubtitleLayerRenderer() : impl_(std::make_unique<Impl>()) {
}

SubtitleLayerRenderer::~SubtitleLayerRenderer() = default;

void SubtitleLayerRenderer::initialize(const RenderJob& job, int video_width, int video_height) {
  impl_->job = job;
  impl_->video_width = video_width;
  impl_->video_height = video_height;
  impl_->use_ass = !job.subtitle_ass.empty() && (job.subtitle_renderer == "auto" || job.subtitle_renderer == "libass" || job.subtitle_renderer == "ass");
  if (impl_->use_ass) {
    ass_renderer_.initialize(job, video_width, video_height);
  } else {
    text_box_renderer_.initialize(job, video_width, video_height);
  }
}

std::vector<SubtitleOverlay> SubtitleLayerRenderer::render(double timestamp_seconds) const {
  std::vector<SubtitleOverlay> overlays;
  if (impl_->use_ass) {
    const SubtitleOverlay overlay = ass_renderer_.render(timestamp_seconds);
    if (overlay.enabled) {
      overlays.push_back(overlay);
    }
    return overlays;
  }

  const std::vector<Region>& configured_regions =
      impl_->job.subtitle_regions.empty() ? impl_->job.regions : impl_->job.subtitle_regions;
  const std::vector<Region> active_regions = collectActiveRegions(configured_regions, timestamp_seconds);
  if (active_regions.empty()) {
    return overlays;
  }

  if (text_box_renderer_.available()) {
    const SubtitleOverlay overlay = text_box_renderer_.render(timestamp_seconds, &active_regions.front());
    if (overlay.enabled) {
      overlays.push_back(overlay);
    }
    return overlays;
  }

  return overlays;
}

bool SubtitleLayerRenderer::available() const {
  return impl_->use_ass ? ass_renderer_.available() : text_box_renderer_.available();
}

}  // namespace video_engine
