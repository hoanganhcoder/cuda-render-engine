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

## Colab Quickstart

Clone and run:

```bash
git clone https://github.com/hoanganhcoder/cuda-render-engine.git
cd cuda-render-engine/video_engine
bash scripts/setup_colab.sh
bash scripts/smoke_test_colab.sh
```

What `setup_colab.sh` does:

- installs `pybind11`, `cmake`, and `ninja`
- installs Linux build dependencies
- builds `nv-codec-headers` pinned for older Colab NVENC driver compatibility
- builds FFmpeg from source into `video_engine/ffmpeg-dev` without requiring `libnpp`
- configures and builds the Python module

What `smoke_test_colab.sh` does:

- sets package/runtime paths and `LD_LIBRARY_PATH`
- downloads a small sample MP4
- imports `video_engine`
- runs a simple render test

Direct Colab import:

```python
import sys
sys.path.insert(0, "/content/cuda-render-engine")

import video_engine
print(video_engine.version())
```

## Python API

```python
import video_engine

job = {
    "input": "input.mp4",
    "output": "output.mp4",
    "subtitle_srt": "examples/sample.srt",
    "subtitle_font_family": "Noto Sans",
    "subtitle_font_size": 48,
    "subtitle_bold": True,
    "subtitle_italic": False,
    "subtitle_text_color": "#FFF200",
    "subtitle_outline_color": "#101010",
    "subtitle_back_color": "#00000000",
    "subtitle_outline": 4,
    "subtitle_shadow": 0,
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

Hard subtitle inputs supported now:

- `subtitle_srt`: path to an `.srt` file
- `subtitle_text`: single always-on text string
- `subtitle_font_family`: preferred font family, e.g. `Noto Sans`, `DejaVu Sans`
- `subtitle_font_path`: optional explicit font file path
- `subtitle_font_size`: font size for libass renderer
- `subtitle_text_color`: main text color in `#RRGGBB` or `#RRGGBBAA`
- `subtitle_outline_color`: outline color in `#RRGGBB` or `#RRGGBBAA`
- `subtitle_back_color`: ASS background/box color in `#RRGGBB` or `#RRGGBBAA`
- `subtitle_bold`: bold text on/off
- `subtitle_italic`: italic text on/off
- `subtitle_outline`: outline thickness
- `subtitle_shadow`: shadow size
- `subtitle_font_scale`: integer bitmap font scale, default `4`
- `subtitle_margin`: inner padding inside the active fill region
- `subtitle_opacity`: subtitle text opacity in `[0, 1]`

Subtitle rendering path:

- preferred path: `libass + FreeType + Fontconfig` for Unicode, bold, italic, outline, shadow, and nicer typography
- fallback path: built-in bitmap renderer when `libass` is unavailable at build time

Recommended "fansub-style" defaults:

- `subtitle_text_color="#FFF200"`
- `subtitle_outline_color="#101010"`
- `subtitle_back_color="#00000000"`
- `subtitle_outline=4`
- `subtitle_shadow=0`

## JSON Job Example

See `examples/job.json` for a full multi-region example. Optional top-level `width`, `height`, and `fps`
fall back to decoder values if omitted or set to `0`.

## Requirements

- NVIDIA GPU + driver available at runtime.
- FFmpeg must be built with CUDA, NVDEC, and NVENC enabled.
- Current zero-copy path expects NVDEC output surfaces in `NV12`.
- Output encoder is currently `h264_nvenc`.
- Colab may need a pinned FFmpeg + `nv-codec-headers` combo to match its driver API.

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
