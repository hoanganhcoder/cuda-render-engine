#include "cuda/CudaContext.h"

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

CudaContext::CudaContext() = default;

CudaContext::~CudaContext() {
  if (stream_) {
    cudaStreamDestroy(stream_);
    stream_ = nullptr;
  }
}

void CudaContext::initialize() {
  if (initialized_) {
    return;
  }
  throwOnCudaError(cudaSetDevice(0), "Failed to select CUDA device");
  throwOnCudaError(cudaStreamCreate(&stream_), "Failed to create CUDA stream");
  initialized_ = true;
}

}  // namespace video_engine
