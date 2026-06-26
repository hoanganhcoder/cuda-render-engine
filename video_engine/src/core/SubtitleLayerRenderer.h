#pragma once

#include <memory>

#include "core/LayerRenderer.h"
#include "core/TextBoxRenderer.h"

namespace video_engine {

class SubtitleLayerRenderer : public LayerRenderer {
public:
  SubtitleLayerRenderer();
  ~SubtitleLayerRenderer();

  SubtitleLayerRenderer(const SubtitleLayerRenderer&) = delete;
  SubtitleLayerRenderer& operator=(const SubtitleLayerRenderer&) = delete;

  void initialize(const RenderJob& job, int video_width, int video_height) override;
  [[nodiscard]] std::vector<SubtitleOverlay> render(double timestamp_seconds) const override;
  [[nodiscard]] bool available() const override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  TextBoxRenderer text_box_renderer_;
};

}  // namespace video_engine
