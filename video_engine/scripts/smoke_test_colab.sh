#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
FFMPEG_LIB_DIR="${FFMPEG_LIB_DIR:-$REPO_ROOT/ffmpeg-dev/lib}"
SAMPLE_URL="${SAMPLE_URL:-https://filesamples.com/samples/video/mp4/sample_640x360.mp4}"
SAMPLE_INPUT="${SAMPLE_INPUT:-$REPO_ROOT/sample.mp4}"
SAMPLE_OUTPUT="${SAMPLE_OUTPUT:-$REPO_ROOT/output.mp4}"

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "Build directory not found: $BUILD_DIR" >&2
  exit 1
fi

if [[ ! -d "$FFMPEG_LIB_DIR" ]]; then
  echo "FFmpeg lib directory not found: $FFMPEG_LIB_DIR" >&2
  exit 1
fi

if [[ ! -f "$SAMPLE_INPUT" ]]; then
  wget -q --show-progress -O "$SAMPLE_INPUT" "$SAMPLE_URL"
fi

export PYTHONPATH="$BUILD_DIR:${PYTHONPATH:-}"
export LD_LIBRARY_PATH="$FFMPEG_LIB_DIR:${LD_LIBRARY_PATH:-}"
export VIDEO_ENGINE_REPO_ROOT="$REPO_ROOT"
export VIDEO_ENGINE_PACKAGE_PARENT="$(dirname "$REPO_ROOT")"

python - <<'PY'
import os
import sys

sys.path.insert(0, os.environ["VIDEO_ENGINE_PACKAGE_PARENT"])
import video_engine

print("video_engine version =", video_engine.version())

repo_root = os.environ["VIDEO_ENGINE_REPO_ROOT"]
job = {
    "video_aspect_ratio": "16:9",
    "output_path": os.path.join(repo_root, "output.mp4"),
    "bg_color": "#000000",
    "tracks": [
        {
            "type": "video",
            "path": os.path.join(repo_root, "sample.mp4"),
            "video_scale": 1.0,
            "flip_horizontal": False,
            "h": "center",
            "v": "center",
            "resize_mode": "fit",
        },
        {
            "type": "subtitle",
            "srt": os.path.join(repo_root, "examples", "sample.srt"),
            "gaussian_blur": True,
            "blur": {
                "strength": 1.0,
                "feather": 18,
                "horizontal_blur": 1.0,
                "vertical_stretch": 1.35,
                "temporal_blend": 0.0,
            },
            "font": "SVN-Rush Hour",
            "font_ttf": r"/content/SVN-Rush Hour.ttf",
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
            "wrap": True,
            "clip": False,
            "auto_fit": True,
            "padding_x": 24,
            "padding_y": 10,
            "align_h": "center",
            "align_v": "middle",
            "opacity": 1.0,
            "regions": [{"start": 0.0, "end": 6.0, "x": 0, "y": 245, "w": 640, "h": 95}],
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
            "back_color": "#00000000",
            "outline": 1,
            "shadow": 0,
            "margin": 24,
            "bounce": True,
            "speed_x": 10.0,
            "speed_y": 7.0,
            "opacity": 0.28,
        },
    ],
}

result = video_engine.render(job)
print("render ok =", result)
print("output exists =", os.path.exists(job["output_path"]))
PY
