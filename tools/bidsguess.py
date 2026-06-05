#!/usr/bin/env python3
# Thin wrapper around tools/reproinx.py that injects --bidsguess so dcm2niix is
# invoked with the legacy hazardous BIDS naming scheme (-f %h) and the post-pass
# applies bidsguess-specific cleanups (discard/ removal, single-volume dwi bvec
# drop, 3D _bold -> _sbref demote, .bidsignore for a/b/c collision files).
#
# All flags and positional arguments accepted by reproinx.py are forwarded
# verbatim; --bidsguess is appended unconditionally. To avoid the bidsguess
# behaviour, invoke reproinx.py directly.

from __future__ import annotations

import sys
from pathlib import Path

# Import the sibling module so a single argparse parser owns the CLI surface.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import reproinx  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if "--bidsguess" not in argv:
        argv.append("--bidsguess")
    return reproinx.main(argv)


if __name__ == "__main__":
    raise SystemExit(main())
