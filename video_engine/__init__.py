from __future__ import annotations

import ctypes
import importlib.util
import os
import sys
from pathlib import Path

_PACKAGE_ROOT = Path(__file__).resolve().parent
_BUILD_CANDIDATES = (
    _PACKAGE_ROOT / "build",
    _PACKAGE_ROOT / "build_ninja",
    _PACKAGE_ROOT / "build_vs",
)
_FFMPEG_LIB_DIR = _PACKAGE_ROOT / "ffmpeg-dev" / "lib"
_NATIVE_BASENAME = "_video_engine_native"


def _preload_linux_ffmpeg_libs() -> None:
    if os.name != "posix" or not _FFMPEG_LIB_DIR.exists():
        return

    preload_order = (
        "libavutil.so",
        "libswresample.so",
        "libswscale.so",
        "libavcodec.so",
        "libavformat.so",
    )
    for lib_name in preload_order:
        lib_path = _FFMPEG_LIB_DIR / lib_name
        if lib_path.exists():
            ctypes.CDLL(str(lib_path), mode=ctypes.RTLD_GLOBAL)


def _configure_windows_dll_search() -> None:
    if os.name != "nt":
        return

    for candidate in (_PACKAGE_ROOT / "ffmpeg-dev" / "bin", _PACKAGE_ROOT / "ffmpeg-dev" / "lib"):
        if candidate.exists():
            os.add_dll_directory(str(candidate))


def _find_native_module() -> Path:
    suffixes = (".so", ".pyd", ".dll", ".dylib")
    for path in sorted(_PACKAGE_ROOT.glob(f"{_NATIVE_BASENAME}*")):
        if path.suffix in suffixes or any(str(path).endswith(s) for s in suffixes):
            return path
    for build_dir in _BUILD_CANDIDATES:
        if not build_dir.exists():
            continue
        for path in sorted(build_dir.glob(f"{_NATIVE_BASENAME}*")):
            if path.suffix in suffixes or any(str(path).endswith(s) for s in suffixes):
                return path
    raise ImportError(
        "Could not locate the native video_engine extension. "
        "Expected a built module like build/_video_engine_native*.so"
    )


def _load_native_module():
    _configure_windows_dll_search()
    _preload_linux_ffmpeg_libs()
    native_path = _find_native_module()
    spec = importlib.util.spec_from_file_location(f"{__name__}.{_NATIVE_BASENAME}", native_path)
    if spec is None or spec.loader is None:
        raise ImportError(f"Failed to create import spec for native module at {native_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


_native = _load_native_module()

render = _native.render
render_frame = _native.render_frame
version = _native.version

__all__ = ["render", "render_frame", "version"]
