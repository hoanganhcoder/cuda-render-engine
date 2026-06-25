#pragma once

#include "core/RenderJob.h"
#include "core/timeline/Sequence.h"

namespace video_engine::timeline {

class RenderJobAdapter {
public:
  static Sequence toSequence(const video_engine::RenderJob& job);
};

}  // namespace video_engine::timeline
