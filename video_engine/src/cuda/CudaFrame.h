#pragma once

#include <cstddef>
#include <cstdint>

#include <cuda_runtime.h>

#include "core/Frame.h"

namespace video_engine {

class CudaFrame {
public:
  CudaFrame() = default;
  ~CudaFrame();

  CudaFrame(const CudaFrame&) = delete;
  CudaFrame& operator=(const CudaFrame&) = delete;

  void allocate(int width, int height);
  void upload(const Frame& frame, cudaStream_t stream);
  void download(Frame& frame, cudaStream_t stream) const;
  void copyFrom(const CudaFrame& other, cudaStream_t stream);
  void clear(cudaStream_t stream);
  void release();

  [[nodiscard]] uint8_t* data() const { return data_; }
  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] size_t sizeBytes() const { return size_bytes_; }
  [[nodiscard]] bool allocated() const { return data_ != nullptr; }

private:
  uint8_t* data_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  size_t size_bytes_ = 0;
};

}  // namespace video_engine
