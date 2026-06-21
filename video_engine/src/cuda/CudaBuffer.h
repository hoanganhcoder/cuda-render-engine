#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <cuda_runtime.h>

namespace video_engine {

class CudaBuffer {
public:
  CudaBuffer() = default;
  ~CudaBuffer();

  CudaBuffer(const CudaBuffer&) = delete;
  CudaBuffer& operator=(const CudaBuffer&) = delete;

  void allocate(size_t size_bytes);
  void upload(const std::vector<uint8_t>& bytes, cudaStream_t stream);
  void release();

  [[nodiscard]] uint8_t* data() const { return data_; }
  [[nodiscard]] size_t sizeBytes() const { return size_bytes_; }
  [[nodiscard]] bool allocated() const { return data_ != nullptr; }

private:
  uint8_t* data_ = nullptr;
  size_t size_bytes_ = 0;
};

}  // namespace video_engine
