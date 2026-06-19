#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FFMPEG_URL_DEFAULT="https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/ffmpeg-master-latest-linux64-gpl-shared.tar.xz"
FFMPEG_URL="${FFMPEG_URL:-$FFMPEG_URL_DEFAULT}"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
FFMPEG_STAGE_DIR="${FFMPEG_STAGE_DIR:-$REPO_ROOT/ffmpeg-dev}"
DOWNLOAD_DIR="${DOWNLOAD_DIR:-/tmp/video-engine-ffmpeg}"
PYBIND11_DIR="${PYBIND11_DIR:-$(python -c 'import pybind11; print(pybind11.get_cmake_dir())')}"

echo "[1/6] Repo root: $REPO_ROOT"
echo "[2/6] Installing Python build tools"
python -m pip install --upgrade pip
python -m pip install pybind11 cmake ninja

echo "[3/6] Verifying CUDA runtime"
if ! command -v nvidia-smi >/dev/null 2>&1; then
  echo "nvidia-smi not found; Colab GPU runtime is required." >&2
  exit 1
fi
nvidia-smi || true

echo "[4/6] Downloading FFmpeg shared build"
rm -rf "$DOWNLOAD_DIR"
mkdir -p "$DOWNLOAD_DIR"
cd "$DOWNLOAD_DIR"
wget -q --show-progress -O ffmpeg-linux64-gpl-shared.tar.xz "$FFMPEG_URL"
tar -xf ffmpeg-linux64-gpl-shared.tar.xz

FFMPEG_EXTRACT_DIR="$(find "$DOWNLOAD_DIR" -maxdepth 1 -mindepth 1 -type d | head -n 1)"
if [[ -z "${FFMPEG_EXTRACT_DIR:-}" ]]; then
  echo "Failed to locate extracted FFmpeg directory." >&2
  exit 1
fi

echo "[5/6] Staging FFmpeg into $FFMPEG_STAGE_DIR"
rm -rf "$FFMPEG_STAGE_DIR"
cp -R "$FFMPEG_EXTRACT_DIR" "$FFMPEG_STAGE_DIR"

echo "[6/6] Configuring and building video_engine"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -Dpybind11_DIR="$PYBIND11_DIR"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo
echo "Build complete."
echo "Next:"
echo "  bash $REPO_ROOT/scripts/smoke_test_colab.sh"
