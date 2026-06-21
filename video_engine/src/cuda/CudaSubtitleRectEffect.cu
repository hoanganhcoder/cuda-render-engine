#include "cuda/CudaSubtitleRectEffect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <stdexcept>

#include <cuda_runtime.h>

namespace video_engine {

namespace {

struct DeviceRegion {
  float start;
  float end;
  int x;
  int y;
  int w;
  int h;
  float strength;
  float feather;
  float vertical_stretch;
  float horizontal_blur;
  float temporal_blend;
};

__constant__ DeviceRegion kDeviceRegions[CudaSubtitleRectEffect::kMaxRegions];

inline void throwOnCudaError(cudaError_t result, const char* message) {
  if (result != cudaSuccess) {
    throw std::runtime_error(std::string(message) + ": " + cudaGetErrorString(result));
  }
}

__device__ float clamp01(float value) {
  return fminf(1.0f, fmaxf(0.0f, value));
}

__device__ int clampInt(int value, int low, int high) {
  return max(low, min(high, value));
}

__device__ uint8_t loadLuma(const uint8_t* plane, int pitch, int width, int height, int x, int y) {
  x = clampInt(x, 0, width - 1);
  y = clampInt(y, 0, height - 1);
  return plane[y * pitch + x];
}

__device__ uchar2 loadChroma(const uint8_t* plane, int pitch, int chroma_width, int chroma_height, int x, int y) {
  x = clampInt(x, 0, chroma_width - 1);
  y = clampInt(y, 0, chroma_height - 1);
  const uint8_t* row = plane + y * pitch + x * 2;
  return make_uchar2(row[0], row[1]);
}

__device__ float softEdgeDistance(int coord, int start, int size) {
  const int end = start + size;
  if (coord < start) {
    return static_cast<float>(start - coord);
  }
  if (coord >= end) {
    return static_cast<float>(coord - end + 1);
  }
  return 0.0f;
}

__device__ int chooseVerticalAnchorY(const DeviceRegion& region, int height) {
  const int top_anchor = region.y - 1;
  if (top_anchor >= 0) {
    return top_anchor;
  }
  const int bottom_anchor = region.y + region.h;
  if (bottom_anchor < height) {
    return bottom_anchor;
  }
  return clampInt(region.y, 0, height - 1);
}

__device__ float edgeMask(const DeviceRegion& region, int x, int y) {
  const float dx = softEdgeDistance(x, region.x, region.w);
  const float dy = softEdgeDistance(y, region.y, region.h);
  const float distance = sqrtf(dx * dx + dy * dy);
  if (region.feather <= 0.0f) {
    return distance <= 0.0f ? 1.0f : 0.0f;
  }
  return clamp01(1.0f - distance / region.feather);
}

__device__ float normalizeByte(uint8_t value) {
  return static_cast<float>(value) / 255.0f;
}

__device__ uint8_t denormalizeByte(float value) {
  return static_cast<uint8_t>(clamp01(value) * 255.0f);
}

__device__ float mixFloat(float a, float b, float amount) {
  return a + (b - a) * amount;
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
  const uint8_t alpha = overlay.alpha_mask[local_y * overlay.stride + local_x];
  return normalizeByte(alpha) * clamp01(overlay.opacity);
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

__device__ float sampleVerticalLuma(
    const uint8_t* source_y,
    int pitch_y,
    int width,
    int height,
    int x,
    int y,
    const DeviceRegion& region) {
  const int anchor_y = chooseVerticalAnchorY(region, height);
  const int spread = max(1, static_cast<int>(region.h * fmaxf(region.vertical_stretch, 0.25f) * 0.5f));

  float accum = 0.0f;
  float total_weight = 0.0f;
  for (int tap = -2; tap <= 2; ++tap) {
    const int sample_y = anchor_y + tap * max(1, spread / 4);
    const float weight = 1.0f / (1.0f + fabsf(static_cast<float>(tap)));
    accum += normalizeByte(loadLuma(source_y, pitch_y, width, height, x, sample_y)) * weight;
    total_weight += weight;
  }
  accum /= fmaxf(total_weight, 1.0f);

  const int blur_radius = max(1, static_cast<int>(region.horizontal_blur * 6.0f));
  float blur_accum = 0.0f;
  float blur_weight = 0.0f;
  for (int sx = -blur_radius; sx <= blur_radius; ++sx) {
    const float weight = 1.0f / (1.0f + fabsf(static_cast<float>(sx)));
    blur_accum += normalizeByte(loadLuma(source_y, pitch_y, width, height, x + sx, anchor_y)) * weight;
    blur_weight += weight;
  }
  blur_accum /= fmaxf(blur_weight, 1.0f);

  return mixFloat(accum, blur_accum, clamp01(region.horizontal_blur));
}

__device__ uchar2 sampleVerticalChroma(
    const uint8_t* source_uv,
    int pitch_uv,
    int width,
    int height,
    int x,
    int y,
    const DeviceRegion& region) {
  const int chroma_width = width / 2;
  const int chroma_height = height / 2;
  const int sample_x = x / 2;
  const int anchor_y = chooseVerticalAnchorY(region, height) / 2;
  return loadChroma(source_uv, pitch_uv, chroma_width, chroma_height, sample_x, anchor_y);
}

__global__ void subtitleRectLumaKernel(
    const uint8_t* source_y,
    const uint8_t* previous_y,
    uint8_t* output_y,
    int pitch_y,
    int width,
    int height,
    int region_count,
    DeviceSubtitleOverlay overlay) {
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= width || y >= height) {
    return;
  }

  float current = normalizeByte(loadLuma(source_y, pitch_y, width, height, x, y));
  for (int i = 0; i < region_count; ++i) {
    const DeviceRegion& region = kDeviceRegions[i];
    const float mask = edgeMask(region, x, y) * clamp01(region.strength);
    if (mask <= 0.0f) {
      continue;
    }

    const float fill_value = sampleVerticalLuma(source_y, pitch_y, width, height, x, y, region);
    float blended = mixFloat(current, fill_value, mask);
    if (previous_y != nullptr && region.temporal_blend > 0.0f) {
      const float previous = normalizeByte(loadLuma(previous_y, pitch_y, width, height, x, y));
      blended = mixFloat(blended, previous, clamp01(region.temporal_blend * mask));
    }
    current = blended;
  }

  const float overlay_alpha = sampleOverlayAlpha(overlay, x, y);
  if (overlay_alpha > 0.0f) {
    current = mixFloat(current, normalizeByte(sampleOverlayMaskValue(overlay.luma_mask, overlay, x, y)), overlay_alpha);
  }

  output_y[y * pitch_y + x] = denormalizeByte(current);
}

__global__ void subtitleRectChromaKernel(
    const uint8_t* source_uv,
    const uint8_t* previous_uv,
    uint8_t* output_uv,
    int pitch_uv,
    int width,
    int height,
    int region_count,
    DeviceSubtitleOverlay overlay) {
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int y = blockIdx.y * blockDim.y + threadIdx.y;
  const int chroma_width = width / 2;
  const int chroma_height = height / 2;
  if (x >= chroma_width || y >= chroma_height) {
    return;
  }

  uchar2 current = loadChroma(source_uv, pitch_uv, chroma_width, chroma_height, x, y);
  float current_u = normalizeByte(current.x);
  float current_v = normalizeByte(current.y);
  const int full_x = x * 2;
  const int full_y = y * 2;
  for (int i = 0; i < region_count; ++i) {
    const DeviceRegion& region = kDeviceRegions[i];
    const float mask = edgeMask(region, full_x, full_y) * clamp01(region.strength);
    if (mask <= 0.0f) {
      continue;
    }

    const uchar2 fill_uv = sampleVerticalChroma(source_uv, pitch_uv, width, height, full_x, full_y, region);
    float blended_u = mixFloat(current_u, normalizeByte(fill_uv.x), mask);
    float blended_v = mixFloat(current_v, normalizeByte(fill_uv.y), mask);
    if (previous_uv != nullptr && region.temporal_blend > 0.0f) {
      const uchar2 previous = loadChroma(previous_uv, pitch_uv, chroma_width, chroma_height, x, y);
      blended_u = mixFloat(blended_u, normalizeByte(previous.x), clamp01(region.temporal_blend * mask));
      blended_v = mixFloat(blended_v, normalizeByte(previous.y), clamp01(region.temporal_blend * mask));
    }
    current_u = blended_u;
    current_v = blended_v;
  }

  const float overlay_alpha = sampleOverlayAlpha(overlay, full_x, full_y);
  if (overlay_alpha > 0.0f) {
    current_u = mixFloat(
        current_u,
        normalizeByte(sampleOverlayMaskValue(overlay.chroma_u_mask, overlay, full_x, full_y)),
        overlay_alpha);
    current_v = mixFloat(
        current_v,
        normalizeByte(sampleOverlayMaskValue(overlay.chroma_v_mask, overlay, full_x, full_y)),
        overlay_alpha);
  }

  uint8_t* row = output_uv + y * pitch_uv + x * 2;
  row[0] = denormalizeByte(current_u);
  row[1] = denormalizeByte(current_v);
}

}  // namespace

void CudaSubtitleRectEffect::apply(
    const AVFrame* source_frame,
    const AVFrame* previous_frame,
    AVFrame* output_frame,
    const std::vector<Region>& active_regions,
    const DeviceSubtitleOverlay& text_overlay,
    cudaStream_t stream) const {
  const int width = source_frame->width;
  const int height = source_frame->height;
  uint8_t* output_y = output_frame->data[0];
  uint8_t* output_uv = output_frame->data[1];
  const uint8_t* source_y = source_frame->data[0];
  const uint8_t* source_uv = source_frame->data[1];
  const uint8_t* previous_y = previous_frame ? previous_frame->data[0] : nullptr;
  const uint8_t* previous_uv = previous_frame ? previous_frame->data[1] : nullptr;

  if (active_regions.empty()) {
    throwOnCudaError(cudaMemcpy2DAsync(
                         output_y,
                         output_frame->linesize[0],
                         source_y,
                         source_frame->linesize[0],
                         static_cast<size_t>(width),
                         static_cast<size_t>(height),
                         cudaMemcpyDeviceToDevice,
                         stream),
                     "Failed to copy luma plane for empty region list");
    throwOnCudaError(cudaMemcpy2DAsync(
                         output_uv,
                         output_frame->linesize[1],
                         source_uv,
                         source_frame->linesize[1],
                         static_cast<size_t>(width),
                         static_cast<size_t>(height / 2),
                         cudaMemcpyDeviceToDevice,
                         stream),
                     "Failed to copy chroma plane for empty region list");
    return;
  }

  std::array<DeviceRegion, kMaxRegions> device_regions{};
  const int region_count = std::min(static_cast<int>(active_regions.size()), kMaxRegions);
  for (int i = 0; i < region_count; ++i) {
    const Region& src = active_regions[static_cast<size_t>(i)];
    device_regions[static_cast<size_t>(i)] = DeviceRegion{
        static_cast<float>(src.start),
        static_cast<float>(src.end),
        src.x,
        src.y,
        src.w,
        src.h,
        src.strength,
        src.feather,
        src.vertical_stretch,
        src.horizontal_blur,
        src.temporal_blend};
  }

  throwOnCudaError(cudaMemcpyToSymbolAsync(
                       kDeviceRegions,
                       device_regions.data(),
                       sizeof(DeviceRegion) * static_cast<size_t>(region_count),
                       0,
                       cudaMemcpyHostToDevice,
                       stream),
                   "Failed to upload active regions to constant memory");

  dim3 block(16, 16);
  dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
  subtitleRectLumaKernel<<<grid, block, 0, stream>>>(
      source_y,
      previous_y,
      output_y,
      source_frame->linesize[0],
      width,
      height,
      region_count,
      text_overlay);
  dim3 chroma_grid(((width / 2) + block.x - 1) / block.x, ((height / 2) + block.y - 1) / block.y);
  subtitleRectChromaKernel<<<chroma_grid, block, 0, stream>>>(
      source_uv,
      previous_uv,
      output_uv,
      source_frame->linesize[1],
      width,
      height,
      region_count,
      text_overlay);
  throwOnCudaError(cudaGetLastError(), "Subtitle rectangle NV12 CUDA kernel failed");
}

}  // namespace video_engine
