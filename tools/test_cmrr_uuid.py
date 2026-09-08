#!/usr/bin/env python3
"""Compile and run the dependency-free CMRR UUID unit test.

Run from the repository root with `python3 tools/test_cmrr_uuid.py`.
"""

from pathlib import Path
import os
import subprocess
import tempfile


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    compiler = os.environ.get("CXX", "c++")
    with tempfile.TemporaryDirectory(prefix="dcm2niix-cmrr-uuid-") as tmp:
        executable = Path(tmp) / "test_cmrr_uuid"
        subprocess.run(
            [
                compiler,
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-pedantic",
                f"-I{root / 'console'}",
                str(root / "tools" / "test_cmrr_uuid.cpp"),
                str(root / "console" / "cJSON.cpp"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
