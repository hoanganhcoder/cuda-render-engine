from __future__ import annotations

import shutil
from pathlib import Path

from setuptools import setup
from setuptools.command.build_py import build_py as _build_py


PACKAGE_NAME = "video_engine"
ROOT = Path(__file__).resolve().parent
PACKAGE_ROOT = ROOT
NATIVE_BASENAME = "_video_engine_native"
BUILD_CANDIDATES = (
    ROOT / "build",
    ROOT / "build_ninja",
    ROOT / "build_vs",
)
VERSION = "0.4.0"


def _find_native_module() -> Path:
    suffixes = (".so", ".pyd", ".dll", ".dylib")
    for candidate_dir in BUILD_CANDIDATES:
        if not candidate_dir.exists():
            continue
        for path in sorted(candidate_dir.glob(f"{NATIVE_BASENAME}*")):
            if path.suffix in suffixes or any(str(path).endswith(suffix) for suffix in suffixes):
                return path
    raise FileNotFoundError(
        "Could not locate a built native module. "
        "Build the project first, for example with `cmake --build build -j`."
    )


class build_py(_build_py):
    def run(self) -> None:
        super().run()
        native_module = _find_native_module()
        target_dir = Path(self.build_lib) / PACKAGE_NAME
        target_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(native_module, target_dir / native_module.name)


setup(
    name=PACKAGE_NAME,
    version=VERSION,
    description="Headless CUDA/FFmpeg video render engine",
    packages=[PACKAGE_NAME],
    package_dir={PACKAGE_NAME: "."},
    include_package_data=True,
    package_data={
        PACKAGE_NAME: [
            "ffmpeg-dev/LICENSE.txt",
            "ffmpeg-dev/bin/*",
            "ffmpeg-dev/lib/*",
            "ffmpeg-dev/lib/pkgconfig/*",
        ]
    },
    cmdclass={"build_py": build_py},
    python_requires=">=3.8",
    zip_safe=False,
)
