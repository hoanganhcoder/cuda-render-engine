#include "cuda/CudaBuffer.h"

#include <string>
#include <stdexcept>

namespace video_engine {

namespace {

void throwOnCudaError(cudaError_t result, const char* message) {
  if (result != cudaSuccess) {
    throw std::runtime_error(std::string(message) + ": " + cudaGetErrorString(result));
  }
}

}  // namespace

CudaBuffer::~CudaBuffer() {
  release();
}

void CudaBuffer::allocate(size_t size_bytes) {
  if (size_bytes == size_bytes_ && data_ != nullptr) {
    return;
  }
  release();
  size_bytes_ = size_bytes;
  if (size_bytes_ > 0) {
    throwOnCudaError(cudaMalloc(reinterpret_cast<void**>(&data_), size_bytes_), "Failed to allocate CUDA buffer");
  }
}

void CudaBuffer::upload(const std::vector<uint8_t>& bytes, cudaStream_t stream) {
  allocate(bytes.size());
  if (!bytes.empty()) {
    throwOnCudaError(cudaMemcpyAsync(data_, bytes.data(), bytes.size(), cudaMemcpyHostToDevice, stream),
                     "Failed to upload subtitle alpha mask to CUDA");
  }
}

void CudaBuffer::release() {
  if (data_) {
    cudaFree(data_);
    data_ = nullptr;
  }
  size_bytes_ = 0;
}

}  // namespace video_engine
