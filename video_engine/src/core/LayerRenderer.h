#pragma once

#include <vector>

#include "core/RenderJob.h"
#include "core/SubtitleOverlay.h"

namespace video_engine {

class LayerRenderer {
public:
  virtual ~LayerRenderer() = default;

  virtual void initialize(const RenderJob& job, int video_width, int video_height) = 0;
  [[nodiscard]] virtual std::vector<SubtitleOverlay> render(double timestamp_seconds) const = 0;
  [[nodiscard]] virtual bool available() const = 0;
};

}  // namespace video_engine
