#!/usr/bin/env python3
"""reproinx — second-pass cross-series corrections for `dcm2niix -f %H`.

Runs dcm2niix in ReproIn one-pass mode, then walks the resulting BIDS tree to
fill in details that the C converter cannot resolve in a single pass. See
REPROIN.md for the full list of issues this script attempts to address.

Currently implemented:
  - per-session `_scans.tsv` aggregated from JSON `AcquisitionDateTime`
  - `B0FieldIdentifier` on fmap sidecars and `B0FieldSource` on target sidecars,
    matched by `ShimSetting` (exact) and the NIfTI affine (rtol=5%), mirroring
    heudiconv's `POPULATE_INTENDED_FOR_OPTS = {"matching_parameters":
    ["ImagingVolume", "Shims"], "criterion": "Closest"}`. We emit modern
    BIDS B0Field* fields rather than the legacy `IntendedFor` list.

Usage:
    reproinx.py <indir> [outdir] [subject] [session]

    indir   — directory of DICOMs (required)
    outdir  — BIDS output root (default: indir)
    subject — passes `-bi <subject>` to dcm2niix (default: derive from PatientID)
    session — passes `-bv <session>` to dcm2niix (default: derive from ProtocolName)
"""

from __future__ import annotations

import argparse
import glob as _globlib  # for glob.escape; we keep Path.glob for matching
import gzip
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional, Sequence

# Cap individual JSON sidecar reads. dcm2niix sidecars are usually < 10 KB;
# anything larger is almost certainly malformed or hostile.
_JSON_MAX_BYTES = 50 * 1024 * 1024  # 50 MB


def _load_json(path: Path) -> dict:
    """Read a JSON sidecar with a size cap; raise if absent/oversized/invalid."""
    if path.stat().st_size > _JSON_MAX_BYTES:
        raise ValueError(f"{path} exceeds {_JSON_MAX_BYTES} byte cap")
    return json.loads(path.read_text())


def _save_json(path: Path, data: dict) -> None:
    """Write a JSON sidecar atomically (tempfile + rename) with trailing newline."""
    fd, tmp = tempfile.mkstemp(prefix=path.name + ".", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w") as f:
            json.dump(data, f, indent=2)
            f.write("\n")
        os.replace(tmp, str(path))
    except Exception:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise

# Heudiconv reproin's POPULATE_INTENDED_FOR_OPTS (ported).
MATCHING_PARAMETERS = ("ImagingVolume", "Shims")

# Regex used by heudiconv to collapse a fmap filename to its group prefix.
# See heudiconv/bids.py:find_fmap_groups.
_FMAP_SUFFIX_RE = re.compile(
    r"(_dir-[A-Za-z0-9]+)?"
    r"(_phase[12])?"
    r"(_phasediff)?"
    r"(_magnitude[12])?"
    r"(_fieldmap)?"
)


def _which_dcm2niix() -> str:
    """Locate the dcm2niix binary. Prefers $DCM2NIIX, then build/bin, then PATH."""
    env = os.environ.get("DCM2NIIX")
    if env and Path(env).is_file() and os.access(env, os.X_OK):
        return env
    here = Path(__file__).resolve().parent.parent
    candidates = [
        here / "build" / "bin" / "dcm2niix",
        here / "bin" / "dcm2niix",
        here / "dcm2niix",
    ]
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return str(c)
    found = shutil.which("dcm2niix")
    if found:
        return found
    raise SystemExit(
        "Cannot find dcm2niix. Set $DCM2NIIX, place it on PATH, or build "
        "the project (its bin will be picked up from build/bin/)."
    )


def _run_dcm2niix(indir: str, outdir: str,
                  subject: Optional[str], session: Optional[str],
                  anonymize: bool) -> None:
    bin_path = _which_dcm2niix()
    # -ba n: keep AcquisitionDateTime in the JSON sidecar so _scans.tsv has
    # real ISO timestamps and the "Closest" fmap-matching tie-breaker has
    # something to compare against. Heudiconv does the same. Pass
    # anonymize=True to suppress this; tie-breaking then degrades to
    # first-compatible.
    cmd = [bin_path, "-f", "%H", "-z", "y", "-w", "1", "-o", outdir]
    if not anonymize:
        cmd.extend(["-ba", "n"])
    if subject:
        cmd.extend(["-bi", subject])
    if session:
        cmd.extend(["-bv", session])
    cmd.append(indir)
    print("+ " + " ".join(cmd), file=sys.stderr)
    subprocess.run(cmd, check=True)


# --- ported helpers from heudiconv/bids.py ----------------------------------

def _strip_fmap_suffix(stem: str) -> str:
    """Collapse a fmap filename stem to its group prefix."""
    return _FMAP_SUFFIX_RE.sub("", stem)


def _find_fmap_groups(fmap_dir: Path) -> dict[str, list[Path]]:
    """Group fmap json files that belong together (phasediff+mag, AP+PA, ...)."""
    fmap_jsons = sorted(fmap_dir.glob("*.json"))
    prefixes = sorted({_strip_fmap_suffix(j.stem) for j in fmap_jsons})
    return {
        prefix: [j for j in fmap_jsons if _strip_fmap_suffix(j.stem) == prefix]
        for prefix in prefixes
    }


def _read_nifti_header(nii_path: Path) -> Optional[bytes]:
    """Return the raw 348-byte NIfTI-1 header (handles .nii and .nii.gz)."""
    try:
        if nii_path.suffix == ".gz":
            with gzip.open(str(nii_path), "rb") as f:
                hdr = f.read(348)
        else:
            with open(str(nii_path), "rb") as f:
                hdr = f.read(348)
    except (OSError, EOFError):
        return None
    return hdr if len(hdr) == 348 else None


def _qform_to_affine(pixdim: tuple, b: float, c: float, d: float,
                     ox: float, oy: float, oz: float) -> list:
    """NIfTI-1 quaternion (qform) → 4x4 affine matrix."""
    qfac = pixdim[0] if pixdim[0] in (-1.0, 1.0) else 1.0
    a = math.sqrt(max(1.0 - b * b - c * c - d * d, 0.0))
    r00 = a * a + b * b - c * c - d * d
    r01 = 2 * b * c - 2 * a * d
    r02 = 2 * b * d + 2 * a * c
    r10 = 2 * b * c + 2 * a * d
    r11 = a * a + c * c - b * b - d * d
    r12 = 2 * c * d - 2 * a * b
    r20 = 2 * b * d - 2 * a * c
    r21 = 2 * c * d + 2 * a * b
    r22 = a * a + d * d - b * b - c * c
    sx, sy, sz = pixdim[1], pixdim[2], qfac * pixdim[3]
    return [
        [r00 * sx, r01 * sy, r02 * sz, ox],
        [r10 * sx, r11 * sy, r12 * sz, oy],
        [r20 * sx, r21 * sy, r22 * sz, oz],
        [0.0, 0.0, 0.0, 1.0],
    ]


def _imaging_volume(json_path: Path):
    """Return [affine, shape3] for the NIfTI matching `json_path` (stdlib only).

    Mirrors nibabel's `get_best_affine`: prefer sform when `sform_code > 0`,
    fall back to qform when `qform_code > 0`, otherwise give up.
    """
    pattern = _globlib.escape(json_path.stem) + ".nii*"
    nifti_candidates = sorted(json_path.parent.glob(pattern))
    if not nifti_candidates:
        return None
    hdr = _read_nifti_header(nifti_candidates[0])
    if hdr is None:
        return None
    # Detect endianness via sizeof_hdr (must be 348 for NIfTI-1).
    if struct.unpack("<i", hdr[:4])[0] == 348:
        bo = "<"
    elif struct.unpack(">i", hdr[:4])[0] == 348:
        bo = ">"
    else:
        # Not a NIfTI-1 header (could be NIfTI-2 with sizeof_hdr = 540).
        return None
    dim = struct.unpack(bo + "8h", hdr[40:56])
    shape3 = [int(dim[1]), int(dim[2]), int(dim[3])]
    pixdim = struct.unpack(bo + "8f", hdr[76:108])
    qform_code = struct.unpack(bo + "h", hdr[252:254])[0]
    sform_code = struct.unpack(bo + "h", hdr[254:256])[0]
    if sform_code > 0:
        srow_x = struct.unpack(bo + "4f", hdr[280:296])
        srow_y = struct.unpack(bo + "4f", hdr[296:312])
        srow_z = struct.unpack(bo + "4f", hdr[312:328])
        affine = [list(srow_x), list(srow_y), list(srow_z),
                  [0.0, 0.0, 0.0, 1.0]]
        return [affine, shape3]
    if qform_code > 0:
        qb, qc, qd = struct.unpack(bo + "3f", hdr[256:268])
        ox, oy, oz = struct.unpack(bo + "3f", hdr[268:280])
        affine = _qform_to_affine(pixdim, qb, qc, qd, ox, oy, oz)
        return [affine, shape3]
    return None


def _key_info(json_path: Path, parameter: str):
    """Mirror heudiconv's get_key_info_for_fmap_assignment for our two params.

    Returns the value list, or None when the parameter cannot be derived. The
    caller treats None as "no key info" and skips matching for this parameter.
    """
    data = _load_json(json_path)
    if parameter == "Shims":
        shim = data.get("ShimSetting")
        return [shim] if shim is not None else None
    if parameter == "ImagingVolume":
        return _imaging_volume(json_path)
    return None


def _allclose(a, b, rtol: float = 0.05, atol: float = 1e-8) -> bool:
    """Pure-Python np.allclose: recurse into nested lists, symmetric tolerance.

    Mirrors numpy: |a-b| <= atol + rtol*max(|a|,|b|), so swapping a and b
    yields the same answer.
    """
    if isinstance(a, (list, tuple)) and isinstance(b, (list, tuple)):
        if len(a) != len(b):
            return False
        return all(_allclose(x, y, rtol, atol) for x, y in zip(a, b))
    if not isinstance(a, (int, float)) or not isinstance(b, (int, float)):
        return a == b
    if math.isnan(a) or math.isnan(b):
        return False
    return abs(a - b) <= atol + rtol * max(abs(a), abs(b))


def _params_match(a, b) -> bool:
    """Heudiconv compatibility test: list-of-strings → equal; else allclose 5%."""
    if a is None or b is None or len(a) != len(b):
        return False
    if all(isinstance(x, str) for x in a) and all(isinstance(x, str) for x in b):
        return list(a) == list(b)
    return all(_allclose(x, y, rtol=0.05) for x, y in zip(a, b))


def _find_compatible(json_path: Path,
                     fmap_groups: dict[str, list[Path]],
                     parameters: Sequence[str]) -> dict[str, list[Path]]:
    """Return the subset of fmap_groups whose first member matches every parameter."""
    info = {p: _key_info(json_path, p) for p in parameters}
    out: dict[str, list[Path]] = {}
    for key, group in fmap_groups.items():
        sample = group[0]
        ok = True
        for p in parameters:
            fm_info = _key_info(sample, p)
            if not _params_match(info[p], fm_info):
                ok = False
                break
        if ok:
            out[key] = group
    return out


# --- timestamp helpers ------------------------------------------------------

def _hms_seconds(value: Optional[str]) -> Optional[float]:
    """Convert a JSON sidecar AcquisitionTime (HH:MM:SS[.fff]) to seconds-of-day."""
    if not value:
        return None
    parts = str(value).split(":")
    if len(parts) != 3:
        return None
    try:
        return float(parts[0]) * 3600 + float(parts[1]) * 60 + float(parts[2])
    except ValueError:
        return None


def _acq_iso(data: dict) -> Optional[str]:
    """Best-effort ISO 8601 acquisition timestamp from a JSON sidecar."""
    adt = data.get("AcquisitionDateTime")
    if adt:
        return adt
    d, t = data.get("AcquisitionDate"), data.get("AcquisitionTime")
    if d and t and len(str(d)) == 8:
        return f"{d[:4]}-{d[4:6]}-{d[6:8]}T{t}"
    return None


def _select_closest(json_path: Path,
                    candidates: dict[str, list[Path]]) -> Optional[str]:
    """Pick the fmap group whose mean AcquisitionTime is closest to json_path's."""
    if not candidates:
        return None
    if len(candidates) == 1:
        return next(iter(candidates))
    target = _hms_seconds(_load_json(json_path).get("AcquisitionTime"))
    if target is None:
        return next(iter(candidates))
    best, best_d = None, float("inf")
    for k, group in candidates.items():
        times = [_hms_seconds(_load_json(p).get("AcquisitionTime"))
                 for p in group]
        times = [t for t in times if t is not None]
        if not times:
            continue
        d = abs(sum(times) / len(times) - target)
        if d < best_d:
            best, best_d = k, d
    return best


# --- per-session passes -----------------------------------------------------

def _is_target_for_fmaps(json_path: Path) -> bool:
    """A non-fmap, non-sbref json that may be paired with an fmap."""
    if json_path.parent.name == "fmap":
        return False
    if json_path.stem.endswith("_sbref"):
        return False
    return True


def _b0_identifier(group_prefix: str) -> str:
    """Stable B0FieldIdentifier label derived from the fmap group prefix."""
    # Strip leading sub- and ses- entities for a shorter identifier; fall back
    # to the raw prefix if stripping fails (entities order varies).
    stripped = re.sub(r"^sub-[A-Za-z0-9]+_?", "", group_prefix)
    stripped = re.sub(r"^ses-[A-Za-z0-9]+_?", "", stripped)
    return stripped or group_prefix


def _populate_b0_fields(session_dir: Path) -> int:
    """Populate B0FieldIdentifier / B0FieldSource across one session.

    Returns the number of fmap groups that received an identifier (0 means no
    fmap dir or no compatible targets).
    """
    fmap_dir = session_dir / "fmap"
    if not fmap_dir.is_dir():
        return 0
    fmap_groups = _find_fmap_groups(fmap_dir)
    if not fmap_groups:
        return 0

    target_jsons = [p for p in session_dir.glob("*/*.json") if _is_target_for_fmaps(p)]
    if not target_jsons:
        return 0

    # Map every target to a (possibly empty) list of compatible fmap-group keys,
    # then narrow ties via the Closest-in-time criterion.
    selected: dict[Path, Optional[str]] = {}
    for jp in target_jsons:
        compat = _find_compatible(jp, fmap_groups, MATCHING_PARAMETERS)
        selected[jp] = _select_closest(jp, compat) if compat else None

    # Stable identifier per fmap group.
    ids = {prefix: _b0_identifier(prefix) for prefix in fmap_groups}

    # Write B0FieldIdentifier on every fmap json that actually got a target.
    used_groups = {sel for sel in selected.values() if sel is not None}
    for prefix, group in fmap_groups.items():
        if prefix not in used_groups:
            continue
        for fm_json in group:
            data = _load_json(fm_json)
            data["B0FieldIdentifier"] = ids[prefix]
            _save_json(fm_json, data)

    # Write B0FieldSource on every target that selected one.
    for jp, sel in selected.items():
        if sel is None:
            continue
        data = _load_json(jp)
        data["B0FieldSource"] = ids[sel]
        _save_json(jp, data)

    return len(used_groups)


def _write_scans_tsv(session_dir: Path) -> None:
    """Aggregate AcquisitionDateTime from JSON sidecars into <sub>[_<ses>]_scans.tsv."""
    rows: list[tuple[str, str]] = []
    for jp in sorted(session_dir.glob("*/*.json")):
        nii_pattern = _globlib.escape(jp.stem) + ".nii*"
        nii = next(iter(jp.parent.glob(nii_pattern)), None)
        if nii is None:
            continue
        try:
            data = _load_json(jp)
        except (OSError, ValueError, json.JSONDecodeError):
            continue
        acq = _acq_iso(data) or "n/a"
        rows.append((str(nii.relative_to(session_dir)), acq))
    if not rows:
        return
    rows.sort(key=lambda r: (r[1] == "n/a", r[1]))
    is_ses = session_dir.name.startswith("ses-")
    sub_dir = session_dir.parent if is_ses else session_dir
    if is_ses:
        scans = session_dir / f"{sub_dir.name}_{session_dir.name}_scans.tsv"
    else:
        scans = session_dir / f"{sub_dir.name}_scans.tsv"
    with scans.open("w") as f:
        f.write("filename\tacq_time\n")
        for fname, acq in rows:
            f.write(f"{fname}\t{acq}\n")


def _walk_sessions(out_root: Path) -> list[Path]:
    """Find every session-equivalent dir under out_root, skipping derivatives/."""
    sessions: list[Path] = []
    for sub in out_root.rglob("sub-*"):
        if not sub.is_dir():
            continue
        # Skip subjects that live under a derivatives/ branch.
        if any(part == "derivatives" for part in sub.parts):
            continue
        ses_dirs = [d for d in sub.iterdir()
                    if d.is_dir() and d.name.startswith("ses-")]
        if ses_dirs:
            sessions.extend(ses_dirs)
        else:
            sessions.append(sub)
    return sessions


def _post_process(out_root: Path, strict: bool) -> None:
    sessions = _walk_sessions(out_root)
    if not sessions:
        print(f"reproinx: no sub-* folders under {out_root}", file=sys.stderr)
        return
    failures = 0
    for ses in sessions:
        try:
            _write_scans_tsv(ses)
            n = _populate_b0_fields(ses)
            print(f"  {ses}: paired {n} fmap group(s)", file=sys.stderr)
        except Exception as e:
            failures += 1
            print(f"reproinx: post-processing failed for {ses}: {e}",
                  file=sys.stderr)
            if strict:
                raise


def main(argv: Optional[list[str]] = None) -> int:
    p = argparse.ArgumentParser(
        description="Run dcm2niix in ReproIn mode and apply second-pass fixes.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    p.add_argument("indir", help="DICOM input directory")
    p.add_argument("outdir", nargs="?",
                   help="BIDS output root (default: indir)")
    p.add_argument("subject", nargs="?",
                   help="BIDS subject (default: derived from PatientID)")
    p.add_argument("session", nargs="?",
                   help="BIDS session (default: derived from ProtocolName)")
    p.add_argument("--no-convert", action="store_true",
                   help="Skip dcm2niix; only run the post-process pass.")
    p.add_argument("--anonymize", action="store_true",
                   help="Use dcm2niix's BIDS anonymisation default (-ba y); "
                        "AcquisitionDateTime is stripped from sidecars and "
                        "fmap matching falls back to first-compatible.")
    p.add_argument("--strict", action="store_true",
                   help="Exit non-zero if any session's post-processing fails. "
                        "Default is to log per-session failures and continue.")
    args = p.parse_args(argv)

    indir = Path(args.indir).resolve()
    outdir = Path(args.outdir).resolve() if args.outdir else indir

    if not indir.is_dir():
        print(f"reproinx: indir does not exist: {indir}", file=sys.stderr)
        return 2
    outdir.mkdir(parents=True, exist_ok=True)

    if not args.no_convert:
        _run_dcm2niix(str(indir), str(outdir), args.subject, args.session,
                      anonymize=args.anonymize)

    _post_process(outdir, strict=args.strict)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
