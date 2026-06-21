#pragma once

#include <memory>
#include <vector>

#include "core/AssSubtitleRenderer.h"
#include "core/RenderJob.h"
#include "core/SubtitleOverlay.h"

namespace video_engine {

class WatermarkRenderer {
public:
  WatermarkRenderer();
  ~WatermarkRenderer();

  WatermarkRenderer(const WatermarkRenderer&) = delete;
  WatermarkRenderer& operator=(const WatermarkRenderer&) = delete;

  void initialize(const RenderJob& job, int video_width, int video_height);
  std::vector<SubtitleOverlay> render(double timestamp_seconds) const;
  [[nodiscard]] bool available() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  AssSubtitleRenderer text_renderer_;
};

}  // namespace video_engine
