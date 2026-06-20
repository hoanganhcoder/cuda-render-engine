import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import video_engine


def main() -> int:
    if len(sys.argv) < 3:
        print("Usage: python examples/test_render.py input.mp4 output.mp4 [job.json]")
        return 1

    input_path = Path(sys.argv[1]).as_posix()
    output_path = Path(sys.argv[2]).as_posix()
    job_path = Path(sys.argv[3]) if len(sys.argv) > 3 else Path(__file__).with_name("job.json")

    with job_path.open("r", encoding="utf-8") as handle:
        job = json.load(handle)

    job["input"] = input_path
    job["output"] = output_path

    print("video_engine version:", video_engine.version())
    success = video_engine.render(job)
    print("render success:", success)
    return 0 if success else 2


if __name__ == "__main__":
    raise SystemExit(main())
