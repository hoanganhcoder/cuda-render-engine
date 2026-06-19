#pragma once

#include <cuda_runtime.h>

namespace video_engine {

class CudaContext {
public:
  CudaContext();
  ~CudaContext();

  CudaContext(const CudaContext&) = delete;
  CudaContext& operator=(const CudaContext&) = delete;

  void initialize();
  [[nodiscard]] cudaStream_t stream() const { return stream_; }

private:
  bool initialized_ = false;
  cudaStream_t stream_ = nullptr;
};

}  // namespace video_engine
