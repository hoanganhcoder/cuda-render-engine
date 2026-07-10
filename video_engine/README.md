# Video Engine

`video_engine` is a headless C++17/CUDA/FFmpeg render engine exposed to Python through `pybind11`.
The current pipeline is GPU-only and zero-copy:

`Input MP4 -> FFmpeg demux -> NVDEC -> CUDA effect -> NVENC -> Output MP4`

## Architecture

- `src/core`: render job parsing, orchestration, logging, region models, render loop.
- `src/core/timeline`: editor-style sequence/layer specs and adapters.
- `src/core`: text-box renderer, blur-box effect, and overlay-layer rendering.
- `src/codec`: FFmpeg hardware decode/encode using `AV_PIX_FMT_CUDA` frames.
- `src/cuda`: CUDA stream/context and the NV12 subtitle-rect effect kernel.
- `src/bindings`: Python entrypoints `video_engine.render(job)` and `video_engine.version()`.

Current render flow:

1. Demux packets with FFmpeg.
2. Decode with NVDEC into CUDA hardware frames.
3. Render text-box overlays for subtitle/overlay layers.
4. Apply blur-box regions directly on GPU NV12 planes.
5. Composite text overlays on top of the blurred GPU frame.
6. Encode with `h264_nvenc` from the processed CUDA frame.
7. Mux MP4 output with FFmpeg.

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
  libcairo2-dev \
  libfontconfig1-dev \
  libfreetype6-dev \
  libharfbuzz-dev \
  libpango1.0-dev \
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

Windows helper:

```bat
cd video_engine
scripts\build_windows.bat
```

Useful flags:

```bat
scripts\build_windows.bat --ninja
scripts\build_windows.bat --vs --debug
scripts\build_windows.bat --wheel
scripts\build_windows.bat --ninja --cuda-root "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6"
```

If you vendor FFmpeg locally, place it under `video_engine/ffmpeg-dev/` with `include/` and `lib/`.

Text rendering backends:

- preferred subtitle text-box path on Linux/Colab: `Pango + Cairo + Fontconfig`
- fallback text-box path: `FreeType + HarfBuzz` (`TextBoxRenderer`)
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

## Build Wheel

After the native module has already been built with CMake, package it into a wheel:

```bash
cd video_engine
bash scripts/build_wheel.sh
```

This produces a wheel in `video_engine/dist/` that bundles:

- the prebuilt native module `_video_engine_native*.so` or `.pyd`
- Python wrapper `video_engine/__init__.py`
- runtime FFmpeg libraries from `video_engine/ffmpeg-dev`

Install on another Colab session:

```bash
pip install /path/to/video_engine/dist/video_engine-0.3.2-*.whl
```

Then use it directly:

```python
import video_engine
print(video_engine.version())
```

## Python API

```python
import video_engine

job = {
    "video_aspect_ratio": "16:9",
    "output_path": "output.mp4",
    "bg_color": "#000000",
    "tracks": [
        {
            "type": "video",
            "path": "input.mp4",
            "video_scale": 1.0,
            "flip_horizontal": False,
            "h": "center",
            "v": "center",
            "resize_mode": "fit",
        },
        {
            "type": "gaussian_blur",
            "strength": 0.85,
            "feather": 36,
            "vertical_stretch": 1.0,
            "horizontal_blur": 0.4,
            "temporal_blend": 0.18,
            "regions": [{"start": 0.0, "end": 9999999.0, "x": 0, "y": 610, "w": 1280, "h": 90}],
        },
        {
            "type": "subtitle",
            "srt": "examples/sample.srt",
            "font": "Noto Sans",
            "size": 13.0,
            "bold": True,
            "italic": True,
            "upper": False,
            "color": "#FFF200",
            "outline_color": "#101010",
            "back_color": "#00000000",
            "outline": 4,
            "shadow": 0,
            "margin": 12,
            "regions": {"x": 0, "y": 610, "w": 1280, "h": 90},
        },
        {
            "type": "watermark",
            "text": "@hoanganhcoder",
            "font": "Noto Sans",
            "size": 5.0,
            "bold": True,
            "italic": True,
            "upper": True,
            "color": "#FFFFFF",
            "outline_color": "#000000",
            "bounce": True,
            "opacity": 0.28,
        },
        {
            "type": "logo",
            "path": "logo.png",
            "scale": 0.16,
            "opacity": 0.24,
            "position": {"x": 0.02, "y": 0.02},
        },
        {
            "type": "image",
            "path": "image.png",
            "w": "100%",
            "h": "100%",
            "resize_mode": "stretch",
        },
        {
            "type": "gaussian_blur",
            "strength": 0.85,
            "feather": 36,
            "vertical_stretch": 1.0,
            "horizontal_blur": 0.4,
            "temporal_blend": 0.18,
            "regions": [{"start": 0.0, "end": 9999999.0, "x": 1000, "y": 0, "w": 280, "h": 90}],
        },
    ],
}

print(video_engine.version())
print(video_engine.render(job))
```

Preview one rendered frame for editor UI:

```python
preview = video_engine.render_frame(job, 12.5)
rgba = preview["rgba"]  # numpy uint8 array, shape: (height, width, 4)
print(preview["timestamp"], rgba.shape)
```

`render_frame` uses the same GPU decode/render/composite path, then downloads only the requested frame as RGBA for preview.
It is intended for UI/editor previews, not the hot encode path.

Track job layout:

- `video_aspect_ratio`: output canvas aspect, for example `16:9` or `9:16`
- `output_path` or `output`: MP4 output path
- `bg_color`: background color for `fit` letterbox/pillarbox areas
- `tracks[*].type="video"`: source video track; requires `path`
- `tracks[*].resize_mode`: `fit`, `fill`, or `stretch`
- `tracks[*].h`: `left|center|right`; `tracks[*].v`: `top|center|bottom`
- `tracks[*].video_scale`: extra center zoom after resize-mode placement
- `tracks[*].flip_horizontal`: mirror source video
- `tracks[*].type="subtitle"`: SRT/text subtitle layer only
- `tracks[*].type="gaussian_blur"`: blur-only regions independent of subtitle text
- `tracks[*].type="watermark"`: moving transparent text watermark
- `tracks[*].type="logo"`: image overlay; `position.x/y` are relative output coordinates
- `tracks[*].type="image"`: PNG/JPG template overlay; supports `w`, `h`, `resize_mode`, `opacity`, `position`
- `subtitle.srt` or `subtitle.text`: legacy subtitle source
- `subtitle.font` or `subtitle.font_ttf`: system font name or explicit `.ttf`
- when using `subtitle.font_ttf`, that file is treated as the exact face; for best results provide the matching bold/italic face instead of relying on synthetic styles
- `video_scale`: zoom from center, `1.0` means no zoom, `1.2` means zoom in 20%
- `flip_horizontal`: mirror the whole video left-to-right
- `subtitle.size`, `watermark.size`: `%` of video height, not pixels
- `subtitle.size` default is `1.5` and `subtitle.italic` default is `true`
- subtitle text is rendered as an editor-style text box inside `subtitle.regions[*]`
- `subtitle.wrap`: wrap to the text box width
- `subtitle.clip`: clip text strictly to the text box bounds; default is `false` so multi-line subtitle text can extend beyond region height while remaining vertically centered around the region
- `subtitle.auto_fit`: shrink text if needed to fit the text box height
- `subtitle.padding_x`, `subtitle.padding_y`: inner box padding
- `subtitle.align_h`: `left|center|right`
- `subtitle.align_v`: `top|middle|bottom`
- `subtitle.upper`, `watermark.upper`: Unicode-aware uppercase before rendering
- `subtitle.bold`, `subtitle.italic`, `subtitle.color`
- `subtitle.outline_color`, `subtitle.back_color`, `subtitle.outline`, `subtitle.shadow`
- `subtitle.regions`: subtitle text box, either one object or a list of objects
- `gaussian_blur.regions`: blur regions with `x/y/w/h/start/end/...`; blur is intentionally separate from subtitle text
- `watermark.text`: transparent text watermark
- `watermark.font` or `watermark.font_ttf`: system font name or explicit `.ttf`
- when using `watermark.font_ttf`, that file is treated as the exact face; for best results provide the matching bold/italic face instead of relying on synthetic styles
- `watermark.color`, `watermark.outline_color`, `watermark.opacity`
- `watermark.bounce`, `watermark.speed_x`, `watermark.speed_y`
- `logo.path`: optional logo image path
- `logo.scale`, `logo.opacity`, `logo.bounce`, `logo.speed_x`, `logo.speed_y`

Subtitle rendering path:

- preferred subtitle text-box path on Linux/Colab: `Pango + Cairo + Fontconfig` for mature layout/shaping of script/display fonts
- fallback text-box path: `TextBoxRenderer (FreeType + HarfBuzz)` for region-based layout and explicit line breaking

Editor-style model:

- tracks are evaluated as separate layers: video base, blur regions, subtitle text, then top overlays
- subtitle/watermark/logo/image overlays are composited after blur, so text stays sharp and blur does not leak through the whole video
- `subtitle.regions[*]` defines the subtitle text box
- text layout uses `wrap/clip/auto_fit/padding/alignment`
- blur is applied independently through the region mask path (`blur box`)
- watermark text and logos are handled by `OverlayLayerRenderer`

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
- The subtitle effect is now a GPU Gaussian blur over the configured subtitle region, not a full compositor yet.
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
