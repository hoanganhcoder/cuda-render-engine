#include "cuda/CudaOverlayCompositeEffect.h"

#include <stdexcept>
#include <string>

extern "C" {
#include <libavutil/pixfmt.h>
}

namespace video_engine {

namespace {

void throwOnCudaError(cudaError_t result, const char* message) {
  if (result != cudaSuccess) {
    throw std::runtime_error(std::string(message) + ": " + cudaGetErrorString(result));
  }
}

__device__ float clamp01(float value) {
  return fminf(fmaxf(value, 0.0f), 1.0f);
}

__device__ float normalizeByte(uint8_t value) {
  return static_cast<float>(value) * (1.0f / 255.0f);
}

__device__ uint8_t denormalizeByte(float value) {
  return static_cast<uint8_t>(clamp01(value) * 255.0f + 0.5f);
}

__device__ float mixFloat(float base, float overlay, float alpha) {
  return base * (1.0f - alpha) + overlay * alpha;
}

__device__ float sampleOverlayAlpha(const DeviceSubtitleOverlay& overlay, int x, int y) {
  if (!overlay.enabled()) {
    return 0.0f;
  }
  if (x < overlay.x || y < overlay.y || x >= overlay.x + overlay.width || y >= overlay.y + overlay.height) {
    return 0.0f;
  }
  const int local_x = x - overlay.x;
  const int local_y = y - overlay.y;
  return normalizeByte(overlay.alpha_mask[local_y * overlay.stride + local_x]) * clamp01(overlay.opacity);
}

__device__ uint8_t sampleOverlayMaskValue(const uint8_t* plane, const DeviceSubtitleOverlay& overlay, int x, int y) {
  if (plane == nullptr || !overlay.enabled()) {
    return 0;
  }
  if (x < overlay.x || y < overlay.y || x >= overlay.x + overlay.width || y >= overlay.y + overlay.height) {
    return 0;
  }
  const int local_x = x - overlay.x;
  const int local_y = y - overlay.y;
  return plane[local_y * overlay.stride + local_x];
}

__device__ bool sampleOverlayChromaAverage(
    const DeviceSubtitleOverlay& overlay,
    int x,
    int y,
    float& alpha,
    float& chroma_u,
    float& chroma_v) {
  if (!overlay.enabled()) {
    return false;
  }

  float alpha_sum = 0.0f;
  float chroma_u_sum = 0.0f;
  float chroma_v_sum = 0.0f;
  for (int dy = 0; dy < 2; ++dy) {
    for (int dx = 0; dx < 2; ++dx) {
      const int sample_x = x + dx;
      const int sample_y = y + dy;
      if (sample_x < overlay.x || sample_y < overlay.y || sample_x >= overlay.x + overlay.width ||
          sample_y >= overlay.y + overlay.height) {
        continue;
      }
      const int local_x = sample_x - overlay.x;
      const int local_y = sample_y - overlay.y;
      const int index = local_y * overlay.stride + local_x;
      const float sample_alpha = normalizeByte(overlay.alpha_mask[index]) * clamp01(overlay.opacity);
      if (sample_alpha <= 0.0f) {
        continue;
      }
      alpha_sum += sample_alpha;
      chroma_u_sum += normalizeByte(overlay.chroma_u_mask[index]) * sample_alpha;
      chroma_v_sum += normalizeByte(overlay.chroma_v_mask[index]) * sample_alpha;
    }
  }

  if (alpha_sum <= 0.0f) {
    return false;
  }
  alpha = clamp01(alpha_sum * 0.25f);
  chroma_u = chroma_u_sum / alpha_sum;
  chroma_v = chroma_v_sum / alpha_sum;
  return true;
}

__global__ void overlayLumaKernel(uint8_t* y_plane, int pitch_y, int width, int height, DeviceSubtitleOverlay overlay) {
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= width || y >= height) {
    return;
  }

  const float overlay_alpha = sampleOverlayAlpha(overlay, x, y);
  if (overlay_alpha <= 0.0f) {
    return;
  }

  uint8_t* pixel = y_plane + y * pitch_y + x;
  const float base = normalizeByte(*pixel);
  const float overlay_value = normalizeByte(sampleOverlayMaskValue(overlay.luma_mask, overlay, x, y));
  *pixel = denormalizeByte(mixFloat(base, overlay_value, overlay_alpha));
}

__global__ void overlayChromaKernel(uint8_t* uv_plane, int pitch_uv, int width, int height, DeviceSubtitleOverlay overlay) {
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int y = blockIdx.y * blockDim.y + threadIdx.y;
  const int chroma_width = width / 2;
  const int chroma_height = height / 2;
  if (x >= chroma_width || y >= chroma_height) {
    return;
  }

  float overlay_alpha = 0.0f;
  float overlay_u = 0.0f;
  float overlay_v = 0.0f;
  if (!sampleOverlayChromaAverage(overlay, x * 2, y * 2, overlay_alpha, overlay_u, overlay_v)) {
    return;
  }

  uint8_t* pixel = uv_plane + y * pitch_uv + x * 2;
  const float base_u = normalizeByte(pixel[0]);
  const float base_v = normalizeByte(pixel[1]);
  pixel[0] = denormalizeByte(mixFloat(base_u, overlay_u, overlay_alpha));
  pixel[1] = denormalizeByte(mixFloat(base_v, overlay_v, overlay_alpha));
}

}  // namespace

void CudaOverlayCompositeEffect::apply(AVFrame* frame, const DeviceSubtitleOverlay& overlay, cudaStream_t stream) const {
  if (!overlay.enabled()) {
    return;
  }
  if (frame == nullptr || frame->format != AV_PIX_FMT_CUDA || frame->data[0] == nullptr || frame->data[1] == nullptr) {
    throw std::runtime_error("Overlay composite expects a CUDA NV12 frame.");
  }

  const dim3 block(16, 16);
  const dim3 luma_grid((frame->width + block.x - 1) / block.x, (frame->height + block.y - 1) / block.y);
  overlayLumaKernel<<<luma_grid, block, 0, stream>>>(
      frame->data[0],
      frame->linesize[0],
      frame->width,
      frame->height,
      overlay);
  throwOnCudaError(cudaGetLastError(), "Failed to launch overlay luma kernel");

  const int chroma_width = frame->width / 2;
  const int chroma_height = frame->height / 2;
  const dim3 chroma_grid((chroma_width + block.x - 1) / block.x, (chroma_height + block.y - 1) / block.y);
  overlayChromaKernel<<<chroma_grid, block, 0, stream>>>(
      frame->data[1],
      frame->linesize[1],
      frame->width,
      frame->height,
      overlay);
  throwOnCudaError(cudaGetLastError(), "Failed to launch overlay chroma kernel");
}

}  // namespace video_engine
