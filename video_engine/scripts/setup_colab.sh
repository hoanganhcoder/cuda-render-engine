#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
FFMPEG_STAGE_DIR="${FFMPEG_STAGE_DIR:-$REPO_ROOT/ffmpeg-dev}"
WORK_DIR="${WORK_DIR:-/tmp/video-engine-colab}"
PYBIND11_DIR=""

NVCODEC_HEADERS_TAG="${NVCODEC_HEADERS_TAG:-n13.0.19.0}"
FFMPEG_GIT_TAG="${FFMPEG_GIT_TAG:-n6.1.2}"
FFMPEG_PREFIX="$FFMPEG_STAGE_DIR"
NPROC_VALUE="${NPROC_VALUE:-$(nproc)}"
CUDA_ARCHITECTURES="${CUDA_ARCHITECTURES:-}"

echo "[1/7] Repo root: $REPO_ROOT"
echo "[2/7] Installing system dependencies"
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y \
  build-essential \
  git \
  pkg-config \
  yasm \
  nasm \
  libcairo2-dev \
  libfontconfig1-dev \
  libfreetype6-dev \
  libharfbuzz-dev \
  libnuma-dev \
  libpango1.0-dev \
  fonts-dejavu-core \
  fonts-noto-core \
  fonts-noto-cjk \
  python3-dev \
  python3-pip

echo "[3/7] Installing Python build tools"
python -m pip install --upgrade pip
python -m pip install pybind11 cmake ninja
PYBIND11_DIR="${PYBIND11_DIR:-$(python -c 'import pybind11; print(pybind11.get_cmake_dir())')}"

echo "[3.5/7] Verifying text layout dependencies"
pkg-config --modversion cairo
pkg-config --modversion pangocairo

echo "[4/7] Verifying CUDA runtime"
if ! command -v nvidia-smi >/dev/null 2>&1; then
  echo "nvidia-smi not found; Colab GPU runtime is required." >&2
  exit 1
fi
nvidia-smi || true
if [[ -z "$CUDA_ARCHITECTURES" ]]; then
  CUDA_ARCHITECTURES="$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null | head -n 1 | tr -d ' .')"
fi
if [[ -z "$CUDA_ARCHITECTURES" ]]; then
  CUDA_ARCHITECTURES="75"
fi
echo "CUDA architectures: $CUDA_ARCHITECTURES"

if ! command -v nvcc >/dev/null 2>&1; then
  echo "nvcc not found; CUDA toolkit is required for the zero-copy build." >&2
  exit 1
fi
nvcc --version || true

echo "[5/7] Building NVENC-compatible FFmpeg"
rm -rf "$WORK_DIR" "$FFMPEG_PREFIX"
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

git clone --depth 1 --branch "$NVCODEC_HEADERS_TAG" https://github.com/FFmpeg/nv-codec-headers.git
make -C nv-codec-headers -j"$NPROC_VALUE"
make -C nv-codec-headers install PREFIX=/usr/local

git clone --depth 1 --branch "$FFMPEG_GIT_TAG" https://github.com/FFmpeg/FFmpeg.git ffmpeg-src
cd ffmpeg-src

./configure \
  --prefix="$FFMPEG_PREFIX" \
  --pkg-config-flags="--static" \
  --extra-cflags="-I/usr/local/include" \
  --extra-ldflags="-L/usr/local/lib" \
  --extra-libs="-lpthread -lm -ldl" \
  --bindir="$FFMPEG_PREFIX/bin" \
  --enable-gpl \
  --enable-nonfree \
  --enable-shared \
  --disable-static \
  --disable-debug \
  --enable-cuda-nvcc \
  --enable-cuvid \
  --enable-nvdec \
  --enable-nvenc

make -j"$NPROC_VALUE"
make install

echo "[6/7] Configuring and building video_engine"
rm -rf "$BUILD_DIR"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CUDA_ARCHITECTURES="$CUDA_ARCHITECTURES" \
  -Dpybind11_DIR="$PYBIND11_DIR"
cmake --build "$BUILD_DIR" -j"$NPROC_VALUE"

echo "[7/7] Ready"
echo
echo "Build complete."
echo "Next:"
echo "  bash $REPO_ROOT/scripts/smoke_test_colab.sh"
