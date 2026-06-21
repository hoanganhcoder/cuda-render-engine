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
    "input": os.path.join(repo_root, "sample.mp4"),
    "output": os.path.join(repo_root, "output.mp4"),
    "subtitle_srt": os.path.join(repo_root, "examples", "sample.srt"),
    "subtitle_font_family": "Noto Sans",
    "subtitle_font_size": 48,
    "subtitle_bold": True,
    "subtitle_italic": False,
    "subtitle_text_color": "#FFF200",
    "subtitle_outline_color": "#101010",
    "subtitle_back_color": "#00000000",
    "subtitle_outline": 4,
    "subtitle_shadow": 0,
    "subtitle_margin": 12,
    "subtitle_opacity": 1.0,
    "regions": [
        {
            "start": 0.0,
            "end": 6.0,
            "x": 0,
            "y": 245,
            "w": 640,
            "h": 95,
            "strength": 0.94,
            "feather": 48,
            "vertical_stretch": 1.0,
            "horizontal_blur": 0.25,
            "temporal_blend": 0.08,
        }
    ],
}

result = video_engine.render(job)
print("render ok =", result)
print("output exists =", os.path.exists(job["output"]))
PY
