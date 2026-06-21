#pragma once

#include <memory>
#include <string>

#include "core/RenderJob.h"
#include "core/Region.h"
#include "core/SubtitleOverlay.h"

namespace video_engine {

class AssSubtitleRenderer {
public:
  AssSubtitleRenderer();
  ~AssSubtitleRenderer();

  AssSubtitleRenderer(const AssSubtitleRenderer&) = delete;
  AssSubtitleRenderer& operator=(const AssSubtitleRenderer&) = delete;

  void initialize(const RenderJob& job, int video_width, int video_height);
  SubtitleOverlay render(double timestamp_seconds, const Region* anchor_region) const;
  [[nodiscard]] bool available() const { return available_; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool available_ = false;
};

}  // namespace video_engine
