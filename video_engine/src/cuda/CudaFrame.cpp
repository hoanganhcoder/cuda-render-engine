#include "cuda/CudaFrame.h"

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

CudaFrame::~CudaFrame() {
  release();
}

void CudaFrame::allocate(int width, int height) {
  const size_t required_size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4U;
  if (required_size == size_bytes_ && data_ != nullptr) {
    width_ = width;
    height_ = height;
    return;
  }
  release();
  width_ = width;
  height_ = height;
  size_bytes_ = required_size;
  throwOnCudaError(cudaMalloc(reinterpret_cast<void**>(&data_), size_bytes_), "Failed to allocate CUDA frame");
}

void CudaFrame::upload(const Frame& frame, cudaStream_t stream) {
  if (!allocated()) {
    allocate(frame.width, frame.height);
  }
  throwOnCudaError(cudaMemcpyAsync(data_, frame.pixels.data(), size_bytes_, cudaMemcpyHostToDevice, stream),
                   "Failed to upload frame to CUDA");
}

void CudaFrame::download(Frame& frame, cudaStream_t stream) const {
  frame.resize(width_, height_);
  throwOnCudaError(cudaMemcpyAsync(frame.pixels.data(), data_, size_bytes_, cudaMemcpyDeviceToHost, stream),
                   "Failed to download frame from CUDA");
  throwOnCudaError(cudaStreamSynchronize(stream), "Failed to synchronize CUDA stream after download");
}

void CudaFrame::copyFrom(const CudaFrame& other, cudaStream_t stream) {
  if (!allocated()) {
    allocate(other.width_, other.height_);
  }
  throwOnCudaError(cudaMemcpyAsync(data_, other.data_, other.size_bytes_, cudaMemcpyDeviceToDevice, stream),
                   "Failed to copy CUDA frame");
}

void CudaFrame::clear(cudaStream_t stream) {
  if (allocated()) {
    throwOnCudaError(cudaMemsetAsync(data_, 0, size_bytes_, stream), "Failed to clear CUDA frame");
    throwOnCudaError(cudaStreamSynchronize(stream), "Failed to synchronize CUDA stream after clear");
  }
}

void CudaFrame::release() {
  if (data_) {
    cudaFree(data_);
    data_ = nullptr;
  }
  width_ = 0;
  height_ = 0;
  size_bytes_ = 0;
}

}  // namespace video_engine
