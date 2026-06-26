#pragma once

#include <memory>
#include <vector>

#include "core/LayerRenderer.h"
#include "core/RenderJob.h"
#include "core/SubtitleOverlay.h"
#include "core/TextBoxRenderer.h"

namespace video_engine {

class OverlayLayerRenderer : public LayerRenderer {
public:
  OverlayLayerRenderer();
  ~OverlayLayerRenderer();

  OverlayLayerRenderer(const OverlayLayerRenderer&) = delete;
  OverlayLayerRenderer& operator=(const OverlayLayerRenderer&) = delete;

  void initialize(const RenderJob& job, int video_width, int video_height) override;
  [[nodiscard]] std::vector<SubtitleOverlay> render(double timestamp_seconds) const override;
  [[nodiscard]] bool available() const override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  TextBoxRenderer text_renderer_;
};

}  // namespace video_engine
