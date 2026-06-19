# Video Engine

`video_engine` is a headless C++17/CUDA/FFmpeg render engine exposed to Python through `pybind11`.
The current pipeline is GPU-only and zero-copy:

`Input MP4 -> FFmpeg demux -> NVDEC -> CUDA effect -> NVENC -> Output MP4`

## Architecture

- `src/core`: render job parsing, orchestration, logging, region models, render loop.
- `src/codec`: FFmpeg hardware decode/encode using `AV_PIX_FMT_CUDA` frames.
- `src/cuda`: CUDA stream/context and the NV12 subtitle-rect effect kernel.
- `src/bindings`: Python entrypoints `video_engine.render(job)` and `video_engine.version()`.

Current render flow:

1. Demux packets with FFmpeg.
2. Decode with NVDEC into CUDA hardware frames.
3. Run the subtitle-rect effect directly on GPU NV12 planes.
4. Encode with `h264_nvenc` from the processed CUDA frame.
5. Mux MP4 output with FFmpeg.

There is no CPU frame upload/download step in the hot path.

## Build

Dependencies on Ubuntu / Colab:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  ninja-build \
  pkg-config \
  ffmpeg \
  libavformat-dev \
  libavcodec-dev \
  libavutil-dev \
  libswscale-dev \
  python3-dev \
  python3-pip \
  nvidia-cuda-toolkit

pip install pybind11 cmake ninja
```

Configure and build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

If you vendor FFmpeg locally, place it under `video_engine/ffmpeg-dev/` with `include/` and `lib/`.
`CMakeLists.txt` prefers that local tree before falling back to `pkg-config`.

Run:

```bash
PYTHONPATH=build python examples/test_render.py input.mp4 output.mp4
```

## Python API

```python
import video_engine

job = {
    "input": "input.mp4",
    "output": "output.mp4",
    "regions": [
        {
            "start": 1.2,
            "end": 4.8,
            "x": 0,
            "y": 610,
            "w": 1280,
            "h": 90,
            "strength": 0.85,
            "feather": 45,
            "vertical_stretch": 1.0,
            "horizontal_blur": 0.25,
            "temporal_blend": 0.18,
        }
    ],
}

print(video_engine.version())
print(video_engine.render(job))
```

## JSON Job Example

See `examples/job.json` for a full multi-region example. Optional top-level `width`, `height`, and `fps`
fall back to decoder values if omitted or set to `0`.

## Requirements

- NVIDIA GPU + driver available at runtime.
- FFmpeg must be built with CUDA, NVDEC, and NVENC enabled.
- Current zero-copy path expects NVDEC output surfaces in `NV12`.
- Output encoder is currently `h264_nvenc`.

## Limitations

- The first zero-copy implementation is tuned for `NV12` surfaces only.
- The subtitle effect is specialized for soft subtitle-region fill/blur, not a full compositor yet.
- Decoder and encoder expect a stable output resolution.
- Cross-platform FFmpeg CUDA builds can differ in hwaccel behavior and available codecs.

## Roadmap

- P010 / 10-bit surfaces.
- HEVC NVENC path.
- More GPU-native effects without format conversion.
- Text renderer.
- Image overlays.
- Transitions.
- Timeline graph / multi-track render graph.
