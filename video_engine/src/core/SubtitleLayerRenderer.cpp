#include "core/SubtitleLayerRenderer.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "core/SubtitleRenderer.h"
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
};

SubtitleLayerRenderer::SubtitleLayerRenderer() : impl_(std::make_unique<Impl>()) {
}

SubtitleLayerRenderer::~SubtitleLayerRenderer() = default;

void SubtitleLayerRenderer::initialize(const RenderJob& job, int video_width, int video_height) {
  impl_->job = job;
  impl_->video_width = video_width;
  impl_->video_height = video_height;
  text_box_renderer_.initialize(job, video_width, video_height);
  ass_subtitle_renderer_.initialize(job, video_width, video_height);
}

std::vector<SubtitleOverlay> SubtitleLayerRenderer::render(double timestamp_seconds) const {
  std::vector<SubtitleOverlay> overlays;
  const std::vector<Region> active_regions = collectActiveRegions(impl_->job.regions, timestamp_seconds);
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

  if (ass_subtitle_renderer_.available()) {
    const SubtitleOverlay overlay = ass_subtitle_renderer_.render(timestamp_seconds, &active_regions.front());
    if (overlay.enabled) {
      overlays.push_back(overlay);
    }
    return overlays;
  }

  if (!impl_->job.subtitle_text.empty()) {
    const SubtitleOverlay overlay = SubtitleRenderer::buildOverlay(
        active_regions.front(),
        impl_->job.subtitle_text,
        impl_->video_width,
        impl_->video_height,
        impl_->job.subtitle_font_scale,
        impl_->job.subtitle_margin,
        impl_->job.subtitle_opacity);
    if (overlay.enabled) {
      overlays.push_back(overlay);
    }
  }

  return overlays;
}

bool SubtitleLayerRenderer::available() const {
  return text_box_renderer_.available() || ass_subtitle_renderer_.available() || !impl_->job.subtitle_text.empty();
}

}  // namespace video_engine
