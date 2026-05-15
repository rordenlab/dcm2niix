#!/usr/bin/env python3
"""reproinx — second-pass cross-series corrections for `dcm2niix -f %H`.

Runs dcm2niix in ReproIn one-pass mode, then walks the resulting BIDS tree to
fill in details that the C converter cannot resolve in a single pass. See
REPROIN.md for the full list of issues this script attempts to address.

Currently implemented:
  - per-subject session backfill: when only one series carries `_ses-X` in
    its protocol (heudiconv convention: the scout), rename and move every
    other non-derivative file in the same subject into a `ses-X/` subtree.
  - per-session `_scans.tsv` aggregated from JSON `AcquisitionDateTime`.
  - `B0FieldIdentifier` on fmap sidecars and `B0FieldSource` on target sidecars,
    matched by `ShimSetting` (exact) and the NIfTI affine (rtol=5%), mirroring
    heudiconv's `POPULATE_INTENDED_FOR_OPTS = {"matching_parameters":
    ["ImagingVolume", "Shims"], "criterion": "Closest"}`. We emit modern
    BIDS B0Field* fields rather than the legacy `IntendedFor` list.
  - BIDS scaffolding: `CHANGES`, `README`, `.bidsignore`, `participants.tsv`
    and `.json`, `scans.json`, per-task `task-X[_acq-Y]_bold.json`, empty
    per-task `_events.tsv` placeholders.

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
    # No "-w 1": dcm2niix's default ADD_SUFFIX (a/b/c) preserves colliding
    # series; the post-pass below renames them to heudiconv-style __dup-NN
    # using the .reproin_provenance.tsv written by the C side.
    cmd = [bin_path, "-f", "%H", "-z", "y", "-o", outdir]
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


_SES_RE = re.compile(r"_ses-([A-Za-z0-9]+)")

# Provenance TSV written by dcm2niix's `-f %H` path (nii_dicom_batch.cpp
# `reproinAppendProvenance`). One row per converted series, columns:
#   StudyInstanceUID  SeriesNumber  ProtocolName  SeriesDescription
#   StudyDescription  OutputStem
_PROVENANCE_TSV = ".reproin_provenance.tsv"


def _load_provenance(study_root: Path) -> list[dict[str, object]]:
    path = study_root / _PROVENANCE_TSV
    if not path.is_file():
        return []
    rows: list[dict[str, object]] = []
    with path.open("r", encoding="utf-8", errors="replace") as f:
        header = f.readline()
        if not header:
            return []
        cols = header.rstrip("\n").split("\t")
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < len(cols):
                continue
            row: dict[str, object] = {k: v for k, v in zip(cols, parts)}
            try:
                row["SeriesNumber"] = int(str(row.get("SeriesNumber", "0")))
            except ValueError:
                row["SeriesNumber"] = 0
            rows.append(row)
    return rows


def _session_from_provenance(rows: list[dict],
                             study_uid: Optional[str] = None) -> Optional[str]:
    """Return the unique `_ses-X` token mentioned in the TSV, or None.

    Mirrors heudiconv: a single series carrying `_ses-X` (typically the scout)
    defines the session for the whole StudyInstanceUID. Ambiguous studies
    (multiple distinct X values) return None so the caller can fall back.
    """
    sessions: set[str] = set()
    for r in rows:
        if study_uid is not None and r.get("StudyInstanceUID") != study_uid:
            continue
        for field in ("ProtocolName", "SeriesDescription"):
            m = _SES_RE.search(r.get(field, ""))
            if m:
                sessions.add(m.group(1))
    return next(iter(sessions)) if len(sessions) == 1 else None


def _walk_subjects(out_root: Path) -> list[Path]:
    """Find every top-level sub-* dir under out_root, skipping derivatives/."""
    subjects: list[Path] = []
    for sub in out_root.rglob("sub-*"):
        if not sub.is_dir():
            continue
        if any(part == "derivatives" for part in sub.parts):
            continue
        # Only top-level subject dirs (parent is not also a sub-*/ses-* match).
        if sub.parent.name.startswith(("sub-", "ses-")):
            continue
        subjects.append(sub)
    return subjects


def _bids_roots(subjects: list[Path]) -> list[Path]:
    """Group subjects by immediate parent; each parent is a distinct BIDS root.

    dcm2niix `-f %H` appends a `<StudyDescription>` hierarchy below `-o`, so
    a single conversion can produce multiple BIDS datasets (one per study)
    under one `-o`. Each must be scaffolded independently — collapsing them
    to their common ancestor would put `participants.tsv`/`task-*.json` at
    a directory that is not actually a BIDS root.
    """
    roots: set[Path] = {s.parent for s in subjects}
    return sorted(roots)


def _propagate_session(sub_dir: Path) -> Optional[str]:
    """Backfill `_ses-X` into all non-derivative files for this subject.

    Heudiconv places `_ses-X` on a single series (typically the scout) and
    propagates it across the StudyInstanceUID. dcm2niix sees each series in
    isolation, so the propagation has to happen on disk.

    We look at every file path under both the main subject tree AND
    `derivatives/.../<sub_dir.name>/...` (the scout is routed there) for a
    `_ses-X` marker. If exactly one X is found, we rename every file under
    the main subject tree that lacks it and move it into `sub-X/ses-X/...`.

    Returns the resolved session label, or None if no propagation happened.
    """
    # Prefer the C-side provenance TSV: it records the original ProtocolName
    # / SeriesDescription for every series, so the `_ses-X` marker is visible
    # even when the carrying series (typically the scout) was dropped by
    # `-i y` or never produced an on-disk filename containing `_ses-`.
    sessions: set[str] = set()
    prov_rows = _load_provenance(sub_dir.parent)
    if prov_rows:
        ses_tsv = _session_from_provenance(prov_rows)
        if ses_tsv:
            sessions.add(ses_tsv)
    # Fallback: scan filenames at the IMMEDIATE subject root only (and the
    # parallel derivatives/scanner/<sub>/ tree where the scout lands).
    # Walking existing ses-X/ subtrees would conflate a previous run with
    # the current one — on a second run for session B, we'd see {A, B} and
    # refuse to propagate B's new files.
    if not sessions:
        for datatype_dir in sub_dir.iterdir():
            if not datatype_dir.is_dir() or datatype_dir.name.startswith("ses-"):
                continue
            for f in datatype_dir.iterdir():
                if not f.is_file():
                    continue
                m = _SES_RE.search(f.name)
                if m:
                    sessions.add(m.group(1))
        deriv_root = _find_derivatives_root(sub_dir)
        if deriv_root is not None:
            for p in deriv_root.rglob("*"):
                if not p.is_file():
                    continue
                for part in p.parts:
                    m = _SES_RE.search(part)
                    if m:
                        sessions.add(m.group(1))
    if len(sessions) != 1:
        return None
    ses = next(iter(sessions))
    target_root = sub_dir / f"ses-{ses}"
    # Per-series atomicity: group files by stem-up-to-extension(s), preflight
    # every target, and skip the whole stem on any collision so a .nii.gz
    # and its .json never end up in different layouts.
    for datatype_dir in sorted(p for p in sub_dir.iterdir() if p.is_dir()):
        if datatype_dir.name.startswith("ses-"):
            continue
        new_datatype = target_root / datatype_dir.name
        groups: dict[str, list[Path]] = {}
        for f in sorted(datatype_dir.iterdir()):
            if not f.is_file():
                continue
            groups.setdefault(_series_stem(f.name), []).append(f)
        new_datatype.mkdir(parents=True, exist_ok=True)
        for stem, files in groups.items():
            planned: list[tuple[Path, Path]] = []
            collision = False
            for f in files:
                new_name = _inject_session_into_name(f.name, sub_dir.name, ses)
                target = new_datatype / new_name
                if target.exists():
                    collision = True
                    break
                planned.append((f, target))
            if collision:
                print(f"reproinx: backfill skipped series '{stem}' "
                      f"(target exists in {new_datatype})", file=sys.stderr)
                continue
            for src, dst in planned:
                os.replace(str(src), str(dst))
        # Remove now-empty datatype dir.
        try:
            datatype_dir.rmdir()
        except OSError:
            pass
    return ses


_BIDS_EXTS = (".nii.gz", ".nii", ".json", ".bval", ".bvec", ".tsv", ".tsv.gz")


def _series_stem(fname: str) -> str:
    """Strip recognised BIDS extensions to get the per-series stem.

    Used to group sidecars (.json) and gradient files (.bval/.bvec) with
    their .nii.gz so backfill moves a series as one unit.
    """
    for ext in _BIDS_EXTS:
        if fname.endswith(ext):
            return fname[: -len(ext)]
    return fname


def _find_derivatives_root(sub_dir: Path) -> Optional[Path]:
    """Locate the parallel derivatives/scanner/<sub_name>/ tree, if any."""
    name = sub_dir.name
    parent = sub_dir.parent
    candidate = parent / "derivatives" / "scanner" / name
    if candidate.is_dir():
        return candidate
    return None


def _inject_session_into_name(fname: str, sub_token: str, ses: str) -> str:
    """Insert `_ses-X` after the `sub-Y` token. No-op if already present."""
    if _SES_RE.search(fname):
        return fname
    prefix = sub_token + "_"
    if fname.startswith(prefix):
        return f"{sub_token}_ses-{ses}_{fname[len(prefix):]}"
    return fname


def _apply_dup_naming(bids_root: Path) -> int:
    """Rename dcm2niix's a/b/c collision suffix to heudiconv `__dup-NN`.

    dcm2niix's default name-conflict mode appends 'a','b','c',… to subsequent
    series that hash to the same BIDS filename, in write order. Heudiconv
    instead emits `__dup-01`,`__dup-02`,… with the **lowest** SeriesNumber
    owning the unsuffixed base. We read the C-side provenance TSV (which has
    SeriesNumber and the as-written OutputStem) and rename accordingly.

    Returns the number of stems renamed. No-op when no collisions exist.

    Note: we only re-order *within* a collision group; we do NOT attempt to
    match heudiconv's ordering across the whole study (heudiconv's order
    depends on its infotodict iteration, not SeriesNumber). Ordering by
    SeriesNumber asc is deterministic and reproducible.
    """
    rows = _load_provenance(bids_root)
    if not rows:
        return 0
    # Index by OutputStem to detect "stem + single lowercase alpha" siblings.
    by_stem: dict[str, dict[str, object]] = {}
    for r in rows:
        stem = str(r.get("OutputStem", ""))
        if stem and stem not in by_stem:
            by_stem[stem] = r
    # Group: for any stem ending in a single lowercase letter whose base also
    # appears in the TSV, mark this stem as a collision sibling of `base`.
    groups: dict[str, list[dict[str, object]]] = {}
    for stem in by_stem:
        if len(stem) < 2:
            continue
        last = stem[-1]
        base = stem[:-1]
        if "a" <= last <= "z" and base in by_stem:
            groups.setdefault(base, []).append(by_stem[stem])
    # Include the base row in each group.
    for base in list(groups.keys()):
        groups[base].append(by_stem[base])

    renamed = 0
    for base, members in groups.items():
        ordered = sorted(members,
                         key=lambda r: int(str(r.get("SeriesNumber", "0")) or "0"))
        # Build (current_stem, target_stem) renames.
        moves: list[tuple[str, str]] = []
        for idx, row in enumerate(ordered):
            current = str(row.get("OutputStem", ""))
            target = base if idx == 0 else f"{base}__dup-{idx:02d}"
            if current != target:
                moves.append((current, target))
        if not moves:
            continue
        # Two-phase rename: stems first → tmp; tmp → target. Prevents an
        # ordering-dependent collision when the renaming would overwrite a
        # not-yet-moved sibling (e.g. lowest SeriesNumber currently at "_a").
        tmp_stems: dict[str, str] = {}
        for i, (current, _target) in enumerate(moves):
            tmp = f"{base}__reproinx-tmp-{i:03d}"
            if _move_stem_files(bids_root / current, bids_root / tmp) > 0:
                tmp_stems[current] = tmp
        for (current, target) in moves:
            tmp = tmp_stems.get(current)
            if tmp is None:
                continue
            n = _move_stem_files(bids_root / tmp, bids_root / target)
            if n > 0:
                renamed += 1
    return renamed


def _move_stem_files(src_stem: Path, dst_stem: Path) -> int:
    """Move all `<src_stem><ext>` files to `<dst_stem><ext>`.

    Operates on the recognised BIDS extensions (.nii.gz, .json, .bval, …).
    Returns the number of files moved. Skips when source missing.
    """
    moved = 0
    src_dir = src_stem.parent
    src_name = src_stem.name
    if not src_dir.is_dir():
        return 0
    dst_dir = dst_stem.parent
    dst_name = dst_stem.name
    dst_dir.mkdir(parents=True, exist_ok=True)
    for ext in _BIDS_EXTS:
        src = src_dir / f"{src_name}{ext}"
        if not src.is_file():
            continue
        dst = dst_dir / f"{dst_name}{ext}"
        os.replace(str(src), str(dst))
        moved += 1
    return moved


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


# --- BIDS scaffolding -------------------------------------------------------

_CHANGES_TEMPLATE = "1.0.0 — Initial release.\n"
_README_TEMPLATE = (
    "Dataset generated by dcm2niix `-f %H` + reproinx.py post-pass.\n"
    "Edit this file to describe the study, contact information, and any\n"
    "deviations from the standard BIDS layout.\n"
)
_BIDSIGNORE_TEMPLATE = "derivatives/\n.heudiconv/\n"
_PARTICIPANTS_JSON = {
    "participant_id": {
        "Description": "Unique participant identifier (BIDS sub- label).",
    },
}
_SCANS_JSON = {
    "filename": {"Description": "Relative path to the scan."},
    "acq_time": {
        "Description": "Acquisition timestamp from DICOM AcquisitionDateTime "
                       "(ISO 8601). 'n/a' when unavailable.",
    },
}


def _write_text_if_absent(path: Path, content: str) -> None:
    if not path.exists():
        path.write_text(content)


def _write_root_scaffolding(out_root: Path) -> None:
    """Write CHANGES, README (no .md), .bidsignore, scans.json at the root."""
    _write_text_if_absent(out_root / "CHANGES", _CHANGES_TEMPLATE)
    _write_text_if_absent(out_root / "README", _README_TEMPLATE)
    _write_text_if_absent(out_root / ".bidsignore", _BIDSIGNORE_TEMPLATE)
    scans_json = out_root / "scans.json"
    if not scans_json.exists():
        _save_json(scans_json, _SCANS_JSON)


def _write_participants(bids_root: Path) -> None:
    """Emit participants.tsv (one row per sub-* in THIS root) and .json.

    Only counts subjects directly under `bids_root`. On re-runs we merge
    new participant rows into the existing file (additive only) so an
    incremental conversion does not leave a stale list.
    """
    subs = sorted({s.name for s in bids_root.iterdir()
                   if s.is_dir() and s.name.startswith("sub-")
                   and not s.name.startswith("sub-.")})
    if not subs:
        return
    tsv = bids_root / "participants.tsv"
    if tsv.exists():
        # Read existing rows; add only the participant_ids we haven't seen.
        existing = tsv.read_text().splitlines()
        if not existing:
            existing = ["participant_id"]
        body = existing[1:]
        first_col = [row.split("\t", 1)[0] for row in body]
        new_rows = [s for s in subs if s not in first_col]
        if new_rows:
            with tsv.open("a") as f:
                for s in new_rows:
                    f.write(f"{s}\n")
    else:
        with tsv.open("w") as f:
            f.write("participant_id\n")
            for s in subs:
                f.write(f"{s}\n")
    pj = bids_root / "participants.json"
    if not pj.exists():
        _save_json(pj, _PARTICIPANTS_JSON)


_TASK_STEM_RE = re.compile(r"(task-[A-Za-z0-9]+(?:_acq-[A-Za-z0-9]+)?)_")


def _emit_task_bold_jsons(out_root: Path) -> None:
    """For every distinct `task-X[_acq-Y]_*_bold.nii.gz`, ensure a top-level
    `task-X[_acq-Y]_bold.json` exists. dcm2niix's default boilerplate emits
    `task-rest_bold.json` only; this catches per-acq variants."""
    stems: set[str] = set()
    for nii in out_root.rglob("*_bold.nii*"):
        if any(part == "derivatives" for part in nii.parts):
            continue
        m = _TASK_STEM_RE.search(nii.name)
        if m:
            stems.add(m.group(1))
    for stem in sorted(stems):
        task_name = stem.split("_acq-")[0][len("task-"):]
        path = out_root / f"{stem}_bold.json"
        if path.exists():
            continue
        _save_json(path, {"TaskName": task_name})
    # Drop dcm2niix's generic `task-<X>_bold.json` when an _acq- variant for
    # the same task is now present. Only remove if the file content matches
    # the known dcm2niix stub (single-key JSON with the matching TaskName);
    # never overwrite a user-curated sidecar.
    for stem in stems:
        if "_acq-" not in stem:
            continue
        task_only = stem.split("_acq-")[0]
        default = out_root / f"{task_only}_bold.json"
        if not default.exists() or default == (out_root / f"{stem}_bold.json"):
            continue
        if _is_dcm2niix_task_stub(default, task_only[len("task-"):]):
            try:
                default.unlink()
            except OSError:
                pass
    # dcm2niix writes README.md; heudiconv writes README. Only remove the
    # .md variant when its content matches dcm2niix's stub — a hand-written
    # README.md must be preserved even if our plain README is also present.
    readme_md = out_root / "README.md"
    readme = out_root / "README"
    if readme.exists() and readme_md.exists() and _is_dcm2niix_readme_stub(readme_md):
        try:
            readme_md.unlink()
        except OSError:
            pass


def _is_dcm2niix_task_stub(path: Path, task_name: str) -> bool:
    """True iff `path` is a generic dcm2niix task JSON stub safe to delete.

    The stub is `{"TaskName": "<task>", "CogAtlasID": "..."}` — exactly two
    keys, both with the expected values. Any other shape indicates a
    user-curated file that we must not touch.
    """
    try:
        data = _load_json(path)
    except (OSError, ValueError, json.JSONDecodeError):
        return False
    if not isinstance(data, dict):
        return False
    if set(data.keys()) - {"TaskName", "CogAtlasID"}:
        return False
    return data.get("TaskName") == task_name


def _is_dcm2niix_readme_stub(path: Path) -> bool:
    """True iff `path` looks like the README.md `createDummyBidsBoilerplate`
    emits ("Generated using dcm2niix (...)... Describe your dataset here...").
    Conservatively short to avoid matching curated READMEs."""
    try:
        text = path.read_text()
    except OSError:
        return False
    if len(text) > 1024:
        return False
    return text.startswith("Generated using dcm2niix") and "Describe your dataset here" in text


def _emit_events_tsv(session_dir: Path) -> None:
    """Write an empty `_events.tsv` placeholder next to each `_bold.nii*`."""
    func_dir = session_dir / "func"
    if not func_dir.is_dir():
        return
    seen: set[str] = set()
    for bold in sorted(func_dir.glob("*_bold.nii*")):
        # task-X_acq-Y_run-NN — strip _echo-N before generating events stem.
        stem = bold.name.split("_bold")[0]
        stem = re.sub(r"_echo-[0-9]+", "", stem)
        if stem in seen:
            continue
        seen.add(stem)
        events = func_dir / f"{stem}_events.tsv"
        if events.exists():
            continue
        events.write_text("onset\tduration\ttrial_type\n")


def _drop_derivatives(out_root: Path) -> int:
    """Remove every `derivatives/` subtree below a study root.

    dcm2niix's `-f %H` routes scouts and DERIVED-flagged images (FA, ColFA,
    TENSOR_B0, scout localizers, …) into `<study>/derivatives/scanner/...`.
    Some callers want a clean main-tree-only BIDS dataset and treat these as
    noise. Returns the number of `derivatives/` trees removed.
    """
    removed = 0
    for root in _bids_roots(_walk_subjects(out_root)):
        d = root / "derivatives"
        if d.is_dir():
            shutil.rmtree(d)
            removed += 1
    return removed


def _post_process(out_root: Path, strict: bool, keep_derivatives: bool = True) -> None:
    # Pass 0: rename dcm2niix's a/b/c collision suffix to heudiconv __dup-NN.
    # Must run before session backfill so the rename happens at the as-written
    # paths recorded in the provenance TSV.
    for root in _bids_roots(_walk_subjects(out_root)):
        try:
            n = _apply_dup_naming(root)
            if n > 0:
                print(f"  {root}: renamed {n} collision suffix(es) to __dup-NN",
                      file=sys.stderr)
        except Exception as e:
            print(f"reproinx: __dup rename failed for {root}: {e}", file=sys.stderr)
            if strict:
                raise
    # Pass 1: session backfill per subject. Must run before scans.tsv so the
    # aggregated paths reflect the post-move layout.
    for sub in _walk_subjects(out_root):
        try:
            resolved = _propagate_session(sub)
            if resolved is not None:
                print(f"  {sub}: propagated _ses-{resolved}", file=sys.stderr)
        except Exception as e:
            print(f"reproinx: session backfill failed for {sub}: {e}",
                  file=sys.stderr)
            if strict:
                raise
    # Pass 2: per-session scans.tsv + B0Field*.
    sessions = _walk_sessions(out_root)
    if not sessions:
        print(f"reproinx: no sub-* folders under {out_root}", file=sys.stderr)
        return
    for ses in sessions:
        try:
            _write_scans_tsv(ses)
            _emit_events_tsv(ses)
            n = _populate_b0_fields(ses)
            print(f"  {ses}: paired {n} fmap group(s)", file=sys.stderr)
        except Exception as e:
            print(f"reproinx: post-processing failed for {ses}: {e}",
                  file=sys.stderr)
            if strict:
                raise
    # Pass 3: root-level scaffolding. dcm2niix appends a StudyDescription
    # hierarchy below -o; a single run can produce multiple BIDS roots
    # (one per StudyDescription). Scaffold each independently — collapsing
    # to their common ancestor would put root metadata at a non-BIDS dir.
    try:
        for root in _bids_roots(_walk_subjects(out_root)):
            _write_root_scaffolding(root)
            _write_participants(root)
            _emit_task_bold_jsons(root)
    except Exception as e:
        print(f"reproinx: scaffolding failed: {e}", file=sys.stderr)
        if strict:
            raise
    # Pass 4 (optional): drop derivatives/ subtree. Must run last so earlier
    # passes (session backfill, fmap pairing) can still consult the scout
    # localizer that lives under derivatives/scanner/.
    if not keep_derivatives:
        try:
            n = _drop_derivatives(out_root)
            if n > 0:
                print(f"  removed derivatives/ from {n} BIDS root(s)",
                      file=sys.stderr)
        except Exception as e:
            print(f"reproinx: derivatives removal failed: {e}", file=sys.stderr)
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
    p.add_argument("--no-derivatives", action="store_true",
                   help="Delete the `derivatives/` subtree under each study "
                        "root after post-processing. dcm2niix routes scouts "
                        "and DERIVED-flagged images (FA, ColFA, TENSOR_B0) "
                        "there; pass this flag for a clean main-tree-only "
                        "BIDS dataset. Default is to keep them.")
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

    _post_process(outdir, strict=args.strict,
                  keep_derivatives=not args.no_derivatives)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
