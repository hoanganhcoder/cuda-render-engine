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

inline int computeBlurRadiusHost(const DeviceRegion& region) {
  const float region_scale = std::max(static_cast<float>(region.h), static_cast<float>(region.w) * 0.15f);
  const float base_radius = region_scale * (0.065f + region.horizontal_blur * 0.14f);
  const float feather_boost = region.feather * 0.04f;
  return std::max(6, std::min(static_cast<int>(base_radius + feather_boost), 24));
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

__device__ float edgeMask(const DeviceRegion& region, int x, int y) {
  const float dx = softEdgeDistance(x, region.x, region.w);
  const float dy = softEdgeDistance(y, region.y, region.h);
  const float distance = fmaxf(dx, dy);
  if (region.feather <= 0.0f) {
    return distance <= 0.0f ? 1.0f : 0.0f;
  }
  const float t = clamp01(distance / region.feather);
  return 1.0f - (t * t * (3.0f - 2.0f * t));
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

__device__ int mapOutputToSourceCoord(int coord, int size, float video_scale, bool flip_horizontal, bool is_x) {
  const float center = (static_cast<float>(size) - 1.0f) * 0.5f;
  float mapped = center + (static_cast<float>(coord) - center) / fmaxf(video_scale, 1.0f);
  if (is_x && flip_horizontal) {
    mapped = static_cast<float>(size - 1) - mapped;
  }
  return clampInt(static_cast<int>(mapped + 0.5f), 0, size - 1);
}

__device__ float gaussianWeight1D(int offset, float sigma) {
  const float value = static_cast<float>(offset);
  return __expf(-0.5f * (value * value) / (sigma * sigma));
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

__global__ void gaussianHorizontalLumaKernel(
    const uint8_t* source_y,
    uint8_t* temp_y,
    int pitch_y,
    int width,
    int height,
    int origin_x,
    int origin_y,
    int roi_width,
    int roi_height,
    float video_scale,
    bool flip_horizontal,
    int blur_radius,
    float sigma_x) {
  const int local_x = blockIdx.x * blockDim.x + threadIdx.x;
  const int local_y = blockIdx.y * blockDim.y + threadIdx.y;
  if (local_x >= roi_width || local_y >= roi_height) {
    return;
  }
  const int x = origin_x + local_x;
  const int y = origin_y + local_y;

  float accum = 0.0f;
  float total_weight = 0.0f;
  for (int dx = -blur_radius; dx <= blur_radius; ++dx) {
    const float weight = gaussianWeight1D(dx, sigma_x);
    const int sample_x = mapOutputToSourceCoord(x + dx, width, video_scale, flip_horizontal, true);
    const int sample_y = mapOutputToSourceCoord(y, height, video_scale, false, false);
    accum += normalizeByte(loadLuma(source_y, pitch_y, width, height, sample_x, sample_y)) * weight;
    total_weight += weight;
  }
  temp_y[y * pitch_y + x] = denormalizeByte(accum / fmaxf(total_weight, 1.0f));
}

__global__ void gaussianVerticalLumaKernel(
    const uint8_t* temp_y,
    const uint8_t* previous_y,
    uint8_t* output_y,
    int pitch_y,
    int width,
    int height,
    int origin_x,
    int origin_y,
    int roi_width,
    int roi_height,
    int region_count,
    int blur_radius_y,
    float sigma_y,
    DeviceSubtitleOverlay overlay) {
  const int local_x = blockIdx.x * blockDim.x + threadIdx.x;
  const int local_y = blockIdx.y * blockDim.y + threadIdx.y;
  if (local_x >= roi_width || local_y >= roi_height) {
    return;
  }
  const int x = origin_x + local_x;
  const int y = origin_y + local_y;

  float current = normalizeByte(loadLuma(temp_y, pitch_y, width, height, x, y));
  float blur_value = current;
  float accum = 0.0f;
  float total_weight = 0.0f;
  for (int dy = -blur_radius_y; dy <= blur_radius_y; ++dy) {
    const float weight = gaussianWeight1D(dy, sigma_y);
    accum += normalizeByte(loadLuma(temp_y, pitch_y, width, height, x, y + dy)) * weight;
    total_weight += weight;
  }
  blur_value = accum / fmaxf(total_weight, 1.0f);

  for (int i = 0; i < region_count; ++i) {
    const DeviceRegion& region = kDeviceRegions[i];
    const float mask = edgeMask(region, x, y) * clamp01(region.strength);
    if (mask <= 0.0f) {
      continue;
    }
    float blended = mixFloat(current, blur_value, mask);
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

__global__ void gaussianHorizontalChromaKernel(
    const uint8_t* source_uv,
    uint8_t* temp_uv,
    int pitch_uv,
    int width,
    int height,
    int origin_x,
    int origin_y,
    int roi_width,
    int roi_height,
    float video_scale,
    bool flip_horizontal,
    int blur_radius,
    float sigma_x) {
  const int chroma_width = width / 2;
  const int chroma_height = height / 2;
  const int local_x = blockIdx.x * blockDim.x + threadIdx.x;
  const int local_y = blockIdx.y * blockDim.y + threadIdx.y;
  if (local_x >= roi_width || local_y >= roi_height) {
    return;
  }
  const int x = origin_x + local_x;
  const int y = origin_y + local_y;

  float accum_u = 0.0f;
  float accum_v = 0.0f;
  float total_weight = 0.0f;
  for (int dx = -blur_radius; dx <= blur_radius; ++dx) {
    const float weight = gaussianWeight1D(dx, sigma_x);
    const int sample_x = mapOutputToSourceCoord(x * 2 + dx * 2, width, video_scale, flip_horizontal, true) / 2;
    const int sample_y = mapOutputToSourceCoord(y * 2, height, video_scale, false, false) / 2;
    const uchar2 sample = loadChroma(source_uv, pitch_uv, chroma_width, chroma_height, sample_x, sample_y);
    accum_u += normalizeByte(sample.x) * weight;
    accum_v += normalizeByte(sample.y) * weight;
    total_weight += weight;
  }

  uint8_t* row = temp_uv + y * pitch_uv + x * 2;
  row[0] = denormalizeByte(accum_u / fmaxf(total_weight, 1.0f));
  row[1] = denormalizeByte(accum_v / fmaxf(total_weight, 1.0f));
}

__global__ void gaussianVerticalChromaKernel(
    const uint8_t* temp_uv,
    const uint8_t* previous_uv,
    uint8_t* output_uv,
    int pitch_uv,
    int width,
    int height,
    int origin_x,
    int origin_y,
    int roi_width,
    int roi_height,
    int region_count,
    int blur_radius_y,
    float sigma_y,
    DeviceSubtitleOverlay overlay) {
  const int chroma_width = width / 2;
  const int chroma_height = height / 2;
  const int local_x = blockIdx.x * blockDim.x + threadIdx.x;
  const int local_y = blockIdx.y * blockDim.y + threadIdx.y;
  if (local_x >= roi_width || local_y >= roi_height) {
    return;
  }
  const int x = origin_x + local_x;
  const int y = origin_y + local_y;

  const int full_x = x * 2;
  const int full_y = y * 2;
  const uchar2 current = loadChroma(temp_uv, pitch_uv, chroma_width, chroma_height, x, y);
  float current_u = normalizeByte(current.x);
  float current_v = normalizeByte(current.y);

  float accum_u = 0.0f;
  float accum_v = 0.0f;
  float total_weight = 0.0f;
  for (int dy = -blur_radius_y; dy <= blur_radius_y; ++dy) {
    const float weight = gaussianWeight1D(dy, sigma_y);
    const uchar2 sample = loadChroma(temp_uv, pitch_uv, chroma_width, chroma_height, x, y + dy);
    accum_u += normalizeByte(sample.x) * weight;
    accum_v += normalizeByte(sample.y) * weight;
    total_weight += weight;
  }
  const float blur_u = accum_u / fmaxf(total_weight, 1.0f);
  const float blur_v = accum_v / fmaxf(total_weight, 1.0f);

  for (int i = 0; i < region_count; ++i) {
    const DeviceRegion& region = kDeviceRegions[i];
    const float mask = edgeMask(region, full_x, full_y) * clamp01(region.strength) * 0.22f;
    if (mask <= 0.0f) {
      continue;
    }
    float blended_u = mixFloat(current_u, blur_u, mask);
    float blended_v = mixFloat(current_v, blur_v, mask);
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

__device__ float sampleGaussianLuma(
    const uint8_t* source_y,
    int pitch_y,
    int width,
    int height,
    int x,
    int y,
    const DeviceRegion& region,
    float video_scale,
    bool flip_horizontal) {
  (void)region;
  const int sample_x = mapOutputToSourceCoord(x, width, video_scale, flip_horizontal, true);
  const int sample_y = mapOutputToSourceCoord(y, height, video_scale, false, false);
  return normalizeByte(loadLuma(source_y, pitch_y, width, height, sample_x, sample_y));
}

__device__ uchar2 sampleGaussianChroma(
    const uint8_t* source_uv,
    int pitch_uv,
    int width,
    int height,
    int x,
    int y,
    const DeviceRegion& region,
    float video_scale,
    bool flip_horizontal) {
  (void)region;
  const int chroma_width = width / 2;
  const int chroma_height = height / 2;
  const int sample_x = mapOutputToSourceCoord(x, width, video_scale, flip_horizontal, true) / 2;
  const int sample_y = mapOutputToSourceCoord(y, height, video_scale, false, false) / 2;
  return loadChroma(source_uv, pitch_uv, chroma_width, chroma_height, sample_x, sample_y);
}

__global__ void subtitleRectLumaKernel(
    const uint8_t* source_y,
    const uint8_t* previous_y,
    uint8_t* output_y,
    int pitch_y,
    int width,
    int height,
    int region_count,
    float video_scale,
    bool flip_horizontal,
    bool gaussian_blur,
    DeviceSubtitleOverlay overlay) {
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= width || y >= height) {
    return;
  }

  const int mapped_x = mapOutputToSourceCoord(x, width, video_scale, flip_horizontal, true);
  const int mapped_y = mapOutputToSourceCoord(y, height, video_scale, false, false);
  float current = normalizeByte(loadLuma(source_y, pitch_y, width, height, mapped_x, mapped_y));
  for (int i = 0; i < region_count; ++i) {
    const DeviceRegion& region = kDeviceRegions[i];
    const float mask = edgeMask(region, x, y) * clamp01(region.strength);
    if (mask <= 0.0f) {
      continue;
    }

    const float blur_value =
        gaussian_blur ? sampleGaussianLuma(source_y, pitch_y, width, height, x, y, region, video_scale, flip_horizontal) : current;
    float blended = mixFloat(current, blur_value, mask);
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
    float video_scale,
    bool flip_horizontal,
    bool gaussian_blur,
    DeviceSubtitleOverlay overlay) {
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int y = blockIdx.y * blockDim.y + threadIdx.y;
  const int chroma_width = width / 2;
  const int chroma_height = height / 2;
  if (x >= chroma_width || y >= chroma_height) {
    return;
  }

  const int mapped_x = mapOutputToSourceCoord(x * 2, width, video_scale, flip_horizontal, true) / 2;
  const int mapped_y = mapOutputToSourceCoord(y * 2, height, video_scale, false, false) / 2;
  uchar2 current = loadChroma(source_uv, pitch_uv, chroma_width, chroma_height, mapped_x, mapped_y);
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

    const uchar2 blur_uv = gaussian_blur
                               ? sampleGaussianChroma(source_uv, pitch_uv, width, height, full_x, full_y, region, video_scale, flip_horizontal)
                               : current;
    const float chroma_mask = mask * 0.35f;
    float blended_u = mixFloat(current_u, normalizeByte(blur_uv.x), chroma_mask);
    float blended_v = mixFloat(current_v, normalizeByte(blur_uv.y), chroma_mask);
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
    float video_scale,
    bool flip_horizontal,
    bool gaussian_blur,
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

  if (region_count > 0) {
    throwOnCudaError(cudaMemcpyToSymbolAsync(
                         kDeviceRegions,
                         device_regions.data(),
                         sizeof(DeviceRegion) * static_cast<size_t>(region_count),
                         0,
                         cudaMemcpyHostToDevice,
                         stream),
                     "Failed to upload active regions to constant memory");
  }

  dim3 block(16, 16);
  dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
  if (gaussian_blur && region_count > 0) {
    subtitleRectLumaKernel<<<grid, block, 0, stream>>>(
        source_y,
        previous_y,
        output_y,
        source_frame->linesize[0],
        width,
        height,
        0,
        video_scale,
        flip_horizontal,
        false,
        text_overlay);
    dim3 base_chroma_grid(((width / 2) + block.x - 1) / block.x, ((height / 2) + block.y - 1) / block.y);
    subtitleRectChromaKernel<<<base_chroma_grid, block, 0, stream>>>(
        source_uv,
        previous_uv,
        output_uv,
        source_frame->linesize[1],
        width,
        height,
        0,
        video_scale,
        flip_horizontal,
        false,
        text_overlay);

    int max_radius_x = 0;
    int max_radius_y = 0;
    float max_sigma_x = 1.0f;
    float max_sigma_y = 1.0f;
    int roi_x0 = width;
    int roi_y0 = height;
    int roi_x1 = 0;
    int roi_y1 = 0;
    for (int i = 0; i < region_count; ++i) {
      const DeviceRegion& region = device_regions[static_cast<size_t>(i)];
      const int radius_x = computeBlurRadiusHost(region);
      const int radius_y = max(2, static_cast<int>(static_cast<float>(radius_x) * fmaxf(region.vertical_stretch, 0.85f) * 0.32f));
      max_radius_x = max(max_radius_x, radius_x);
      max_radius_y = max(max_radius_y, radius_y);
      max_sigma_x = fmaxf(max_sigma_x, fmaxf(2.8f, static_cast<float>(radius_x) * 0.72f));
      max_sigma_y = fmaxf(max_sigma_y, fmaxf(1.6f, static_cast<float>(radius_y) * 0.90f));
      const int expand_x = max(2, static_cast<int>(region.feather) + radius_x * 2);
      const int expand_y = max(2, static_cast<int>(region.feather * 0.75f) + max(2, radius_y * 2));
      roi_x0 = min(roi_x0, max(0, region.x - expand_x));
      roi_y0 = min(roi_y0, max(0, region.y - expand_y));
      roi_x1 = max(roi_x1, min(width, region.x + region.w + expand_x));
      roi_y1 = max(roi_y1, min(height, region.y + region.h + expand_y));
    }

    if (roi_x1 <= roi_x0 || roi_y1 <= roi_y0) {
      throwOnCudaError(cudaGetLastError(), "Subtitle rectangle NV12 CUDA kernel failed");
      return;
    }

    const int roi_width = roi_x1 - roi_x0;
    const int roi_height = roi_y1 - roi_y0;
    dim3 roi_grid((roi_width + block.x - 1) / block.x, (roi_height + block.y - 1) / block.y);

    temp_luma_.allocate(static_cast<size_t>(source_frame->linesize[0]) * static_cast<size_t>(height));
    temp_chroma_.allocate(static_cast<size_t>(source_frame->linesize[1]) * static_cast<size_t>(height / 2));

    gaussianHorizontalLumaKernel<<<roi_grid, block, 0, stream>>>(
        source_y,
        temp_luma_.data(),
        source_frame->linesize[0],
        width,
        height,
        roi_x0,
        roi_y0,
        roi_width,
        roi_height,
        video_scale,
        flip_horizontal,
        max_radius_x,
        max_sigma_x);
    gaussianVerticalLumaKernel<<<roi_grid, block, 0, stream>>>(
        temp_luma_.data(),
        previous_y,
        output_y,
        source_frame->linesize[0],
        width,
        height,
        roi_x0,
        roi_y0,
        roi_width,
        roi_height,
        region_count,
        max_radius_y,
        max_sigma_y,
        text_overlay);

    const int chroma_roi_x0 = roi_x0 / 2;
    const int chroma_roi_y0 = roi_y0 / 2;
    const int chroma_roi_x1 = (roi_x1 + 1) / 2;
    const int chroma_roi_y1 = (roi_y1 + 1) / 2;
    const int chroma_roi_width = max(0, chroma_roi_x1 - chroma_roi_x0);
    const int chroma_roi_height = max(0, chroma_roi_y1 - chroma_roi_y0);
    dim3 chroma_grid(
        (chroma_roi_width + block.x - 1) / block.x,
        (chroma_roi_height + block.y - 1) / block.y);
    const int chroma_radius_x = max(1, (max_radius_x + 1) / 3);
    const int chroma_radius_y = max(1, (max_radius_y + 1) / 3);
    if (chroma_roi_width > 0 && chroma_roi_height > 0) {
      gaussianHorizontalChromaKernel<<<chroma_grid, block, 0, stream>>>(
          source_uv,
          temp_chroma_.data(),
          source_frame->linesize[1],
          width,
          height,
          chroma_roi_x0,
          chroma_roi_y0,
          chroma_roi_width,
          chroma_roi_height,
          video_scale,
          flip_horizontal,
          chroma_radius_x,
          fmaxf(1.0f, static_cast<float>(chroma_radius_x) * 0.8f));
      gaussianVerticalChromaKernel<<<chroma_grid, block, 0, stream>>>(
          temp_chroma_.data(),
          previous_uv,
          output_uv,
          source_frame->linesize[1],
          width,
          height,
          chroma_roi_x0,
          chroma_roi_y0,
          chroma_roi_width,
          chroma_roi_height,
          region_count,
          chroma_radius_y,
          fmaxf(1.0f, static_cast<float>(chroma_radius_y) * 0.8f),
          text_overlay);
    }
  } else {
    subtitleRectLumaKernel<<<grid, block, 0, stream>>>(
        source_y,
        previous_y,
        output_y,
        source_frame->linesize[0],
        width,
        height,
        region_count,
        video_scale,
        flip_horizontal,
        gaussian_blur,
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
        video_scale,
        flip_horizontal,
        gaussian_blur,
        text_overlay);
  }
  throwOnCudaError(cudaGetLastError(), "Subtitle rectangle NV12 CUDA kernel failed");
}

}  // namespace video_engine
