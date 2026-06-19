#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace video_engine {

struct Frame {
  int width = 0;
  int height = 0;
  int64_t pts = 0;
  double time_seconds = 0.0;
  std::vector<uint8_t> pixels;

  Frame() = default;
  Frame(int w, int h) { resize(w, h); }

  void resize(int w, int h) {
    width = w;
    height = h;
    pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4U);
  }

  [[nodiscard]] bool empty() const { return pixels.empty(); }
  [[nodiscard]] size_t byteSize() const { return pixels.size(); }
};

}  // namespace video_engine
