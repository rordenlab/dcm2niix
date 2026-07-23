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
  - BIDS scaffolding: `CHANGES`, `README`, `participants.tsv`
    and `.json`, `scans.json`, per-task `task-X[_acq-Y]_bold.json`, empty
    per-task `_events.tsv` placeholders. (`.bidsignore` is created on
    demand by the collision-suffix / single-volume-DWI / bidsguess-residual
    sweeps when there is something real to ignore.)

Usage:
    reproinx.py <indir> [outdir] [subject] [session]

    indir   — directory of DICOMs (required)
    outdir  — BIDS output root (default: indir)
    subject — passes `-bi <subject>` to dcm2niix (default: derive from PatientID)
    session — passes `-bv <session>` to dcm2niix (default: derive from ProtocolName)
"""

from __future__ import annotations

import argparse
import datetime as _dt
import glob as _globlib  # for glob.escape; we keep Path.glob for matching
import gzip
import hashlib
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
    """Write a JSON sidecar atomically (tempfile + rename). No trailing newline
    so the output is byte-identical to heudiconv's reproin scaffolding."""
    fd, tmp = tempfile.mkstemp(prefix=path.name + ".", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w") as f:
            json.dump(data, f, indent=2)
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
                  anonymize: bool) -> int:
    bin_path = _which_dcm2niix()
    # -ba o: omit PII (PatientName/ID/BirthDate/Sex/Age/Size/Weight,
    # AccessionNumber, ReferringPhysicianName) but keep AcquisitionDateTime
    # so scans.tsv has real ISO timestamps and the fmap "Closest"
    # tie-breaker has something to compare against. Heudiconv keeps dates
    # similarly but never emits the patient block. Pass anonymize=True to
    # upgrade to `-ba y` (also strips dates); tie-breaking then degrades to
    # first-compatible.
    # No "-w 1": dcm2niix's default ADD_SUFFIX (a/b/c) preserves colliding
    # series; the post-pass below renames them to heudiconv-style __dup-NN
    # using the .reproin_provenance.tsv written by the C side.
    cmd = [bin_path, "-f", "%H", "-z", "y", "-o", outdir]
    cmd.extend(["-ba", "y" if anonymize else "o"])
    if subject:
        cmd.extend(["-bi", subject])
    if session:
        cmd.extend(["-bv", session])
    cmd.append(indir)
    print("+ " + " ".join(cmd), file=sys.stderr)
    # kEXIT_SOME_OK_SOME_BAD=8 and kEXIT_INCOMPLETE_VOLUMES_FOUND=10 mean
    # partial-success: most files converted, a few failed. The post-pass
    # can still run against what landed on disk. Other non-zero codes
    # (input/output folder errors, no valid files, invalid params, ...)
    # are real failures and should propagate.
    PARTIAL_OK = (8, 10)
    proc = subprocess.run(cmd, check=False)
    if proc.returncode != 0 and proc.returncode not in PARTIAL_OK:
        raise subprocess.CalledProcessError(proc.returncode, cmd)
    if proc.returncode in PARTIAL_OK:
        print(f"dcm2niix exited {proc.returncode} (partial conversion); "
              "continuing with post-pass.", file=sys.stderr)
    # Propagate the partial-success code so automation can distinguish
    # "every series converted cleanly" from "some series failed". Callers
    # that want post-pass output regardless can still consume the BIDS tree
    # on disk; they just shouldn't treat reproinx.py exit==0 as a contract
    # that nothing failed.
    return proc.returncode


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
    """A non-fmap json that may be paired with an fmap.

    sbref IS a valid target: a single-band reference shares its bold's readout
    and thus the same B0 distortion, so it must reference the same fieldmap
    (matches BIDScoin). It pairs via the same shim/PE matching as any target;
    `_populate_b0_fields` then forces each sbref to share its bold sibling's
    selection so a two-compatible-fmap closest-in-time tie can never diverge the
    pair."""
    if json_path.parent.name == "fmap":
        return False
    if json_path.stem.endswith("_noRF"):
        return False  # RF-off noise: nothing imaged, so no distortion to correct
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

    # An sbref shares its bold sibling's readout, hence the same distortion
    # field — but independent closest-in-time selection could straddle two
    # shim-compatible fmap groups and pick different ones for the pair (their
    # AcquisitionTimes differ). Force the pair to ONE shared selection by this
    # precedence: (1) the bold's choice when the bold is timed (its closest-time
    # pick is meaningful and bold-wins is stable/back-compatible); (2) else the
    # sbref's choice when the sbref is timed; (3) else (neither timed, both only
    # fell back to "first compatible") the non-None one, preferring the bold — so
    # an incomplete sbref that matched no fmap cannot erase the bold's valid
    # B0FieldSource.
    for jp in list(selected):
        if not jp.name.endswith("_sbref.json"):
            continue
        sib = jp.with_name(jp.name[: -len("_sbref.json")] + "_bold.json")
        if sib not in selected:
            continue
        if _hms_seconds(_load_json(sib).get("AcquisitionTime")) is not None:
            shared = selected[sib]
        elif _hms_seconds(_load_json(jp).get("AcquisitionTime")) is not None:
            shared = selected[jp]
        else:
            shared = selected[sib] if selected[sib] is not None else selected[jp]
        selected[jp] = shared
        selected[sib] = shared

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


def _series_randstr(data: dict) -> str:
    """8-hex md5 of (StudyInstanceUID || SeriesInstanceUID), heudiconv-style.

    Heudiconv hashes the sorted concatenation of every `*UID` attribute on the
    first DICOM of the series. dcm2niix's sidecar only exposes Study/Series
    UIDs, but those two together are already unique-per-series and
    deterministic, so the resulting randstr is reproducible (matching
    heudiconv's "same randstr for echo-1/echo-2 of the same series") even if
    not byte-identical to heudiconv's bytes.
    """
    pieces: list[str] = []
    for key in ("SeriesInstanceUID", "StudyInstanceUID"):
        v = data.get(key)
        if v:
            pieces.append(str(v))
    if not pieces:
        return "n/a"
    return hashlib.md5("".join(sorted(pieces)).encode()).hexdigest()[:8]


def _load_bidsignore_patterns(bids_root: Path) -> list[str]:
    """Return non-empty, non-comment lines from `<bids_root>/.bidsignore`.
    Used by scans.tsv (and any other writer) to skip files the user has
    asked the validator to ignore; otherwise scans.tsv would reference
    .bidsignored files and trip SCANS_FILENAME_NOT_MATCH_DATASET."""
    p = bids_root / ".bidsignore"
    if not p.is_file():
        return []
    patterns: list[str] = []
    try:
        for raw in p.read_text(encoding="utf-8").splitlines():
            s = raw.strip()
            if s and not s.startswith("#"):
                patterns.append(s)
    except OSError:
        return []
    return patterns


def _is_bidsignored(rel_posix: str, patterns: list[str]) -> bool:
    """Test a root-relative POSIX path against .bidsignore patterns. Patterns
    follow the BIDS validator convention: gitignore-style globs, matched
    against either the basename or the full root-relative path."""
    import fnmatch
    basename = rel_posix.rsplit("/", 1)[-1]
    for pat in patterns:
        if fnmatch.fnmatch(rel_posix, pat) or fnmatch.fnmatch(basename, pat):
            return True
    return False


def _bids_root_for_session(session_dir: Path) -> Path:
    """Return the BIDS root above a session-equivalent directory. Handles
    both `<root>/sub-X/ses-Y/` (root = ses.parent.parent) and the sessionless
    `<root>/sub-X/` (root = ses.parent). The previous `session_dir.parent.parent`
    one-liner silently returned `<root>'s parent` for sessionless trees, which
    broke .bidsignore lookups and singlevol-DWI pattern paths."""
    if session_dir.name.startswith("ses-"):
        return session_dir.parent.parent
    return session_dir.parent


def _write_scans_tsv(session_dir: Path) -> None:
    """Aggregate AcquisitionDateTime + per-series randstr from JSON sidecars
    into `<sub>[_<ses>]_scans.tsv`. Columns mirror heudiconv reproin:
    `filename, acq_time, operator, randstr`. `operator` is always 'n/a' —
    PerformingPhysicianName/OperatorsName carry real human names and are
    intentionally not threaded through dcm2niix's provenance TSV. Users who
    want them can populate the column manually post-conversion.

    NIfTI files matching `.bidsignore` patterns at the bids-root are skipped
    so the scans.tsv doesn't reference files the validator was told to
    ignore (which would otherwise emit SCANS_FILENAME_NOT_MATCH_DATASET).
    """
    bids_root = _bids_root_for_session(session_dir)
    ignore_patterns = _load_bidsignore_patterns(bids_root)
    rows: list[tuple[str, str, str]] = []  # (filename, acq, randstr)
    for jp in sorted(session_dir.glob("*/*.json")):
        nii_pattern = _globlib.escape(jp.stem) + ".nii*"
        nii = next(iter(jp.parent.glob(nii_pattern)), None)
        if nii is None:
            continue
        if ignore_patterns:
            try:
                rel = nii.relative_to(bids_root).as_posix()
            except ValueError:
                rel = nii.name
            if _is_bidsignored(rel, ignore_patterns):
                continue
        try:
            data = _load_json(jp)
        except (OSError, ValueError, json.JSONDecodeError):
            continue
        acq = _acq_iso(data) or "n/a"
        rand = _series_randstr(data)
        # POSIX-style separators in scans.tsv per the BIDS spec — even on
        # Windows the filename column must use forward slashes.
        rows.append((nii.relative_to(session_dir).as_posix(), acq, rand))
    if not rows:
        return
    rows.sort(key=lambda r: (r[1] == "n/a", r[1]))
    is_ses = session_dir.name.startswith("ses-")
    sub_dir = session_dir.parent if is_ses else session_dir
    if is_ses:
        scans = session_dir / f"{sub_dir.name}_{session_dir.name}_scans.tsv"
    else:
        scans = session_dir / f"{sub_dir.name}_scans.tsv"
    # CRLF line endings match heudiconv's csv.writer default (Python's csv
    # module emits dialect='excel-tab' with `\r\n`). participants.tsv is
    # hand-written there and uses LF, so the two TSV files have different
    # conventions — keep them consistent with the reference.
    with scans.open("w", newline="") as f:
        f.write("filename\tacq_time\toperator\trandstr\r\n")
        for fname, acq, rand in rows:
            f.write(f"{fname}\t{acq}\tn/a\t{rand}\r\n")


_SES_RE = re.compile(r"_ses-([A-Za-z0-9]+)")
# Reproin `ses-<X>` token as it appears in a *protocol name*: at the start of
# the string or after an underscore. heudiconv (and therefore the C parser)
# only honours `ses-` when the protocol leads with a known <datatype> token, so
# session-first protocols like "ses-pre_task-bernd_bold" fail to parse and the
# session is not stamped into the on-disk filename. The Unknown-rescue uses
# this to recover the session for such series.
_SES_PROTOCOL_RE = re.compile(r"(?:^|_)ses-([A-Za-z0-9]+)")
# dcm2niix's BidsGuess seeds a `_run-<SeriesNumber>` entity (a raw acquisition
# counter, NOT a BIDS run index). This matches that token for stripping.
_RUN_ENTITY_RE = re.compile(r"_run-[0-9]+")
# A *numeric* reproin `run-<N>` token in a protocol name (leading or mid). Only
# numeric runs are honoured: a non-numeric `run-<label>` (e.g. `run-sedley`) is
# a free-form label, not a BIDS run index, so it is left to collision numbering.
_RUN_PROTOCOL_RE = re.compile(r"(?:^|_)run-([0-9]+)")
# BIDS entities that follow `run` in the canonical filename order. `run` must be
# inserted before the first of these (and before the bare suffix word).
_POST_RUN_ENTITIES = frozenset({
    "echo", "flip", "inv", "mt", "part", "proc", "mod", "recording", "chunk"})


def _insert_run_entity(entity_suffix: str, run_token: str) -> str:
    """Insert `run_token` (e.g. "run-01") into a leading-underscore entity
    suffix at its canonical BIDS position: after sub/ses/task/acq/.../dir and
    before the first post-run entity (part/echo/...) or the trailing suffix
    word. `entity_suffix` has the shape "[_<entity>-<value>]*_<suffix>".

    Idempotent: ANY existing `run-…` token (numeric or label) is dropped before
    inserting — the inserted token fully supersedes a prior run, so a caller that
    builds a collision `run_template` from a suffix that already carries an
    operator-specified run cannot emit a double `_run-X_run-Y`."""
    toks = [t for t in entity_suffix.split("_")
            if t and not t.startswith("run-")]
    insert_at = len(toks) - 1 if toks else 0  # default: just before the suffix
    for i, t in enumerate(toks):
        key = t.split("-", 1)[0] if "-" in t else None
        if key in _POST_RUN_ENTITIES:
            insert_at = i
            break
    toks.insert(max(insert_at, 0), run_token)
    return "_" + "_".join(toks)


# Tokens used to match an Unknown/ physio recording to its parent BOLD.
_TASK_TOKEN_RE = re.compile(r"(?:^|_)task-([A-Za-z0-9]+)")
_ECHO_TOKEN_RE = re.compile(r"_echo-[0-9]+")


def _physio_match_key(stem: str) -> tuple:
    """(task, run) match key for pairing a physio recording with its parent
    BOLD. Run is zero-padded to mirror the rescue's run normalisation so a
    physio protocol `run-1` matches a bold named `run-01`.

    Session is deliberately NOT part of the key: a sessionless failed protocol
    has its BOLD rescued under a fallback timestamp session that the physio
    filename never carried, so a session-bearing key would never match. The
    BOLD's actual session is adopted from its on-disk path, and the (subject-
    scoped) UNIQUE-match requirement plus a same-session subject keeps the
    pairing correct; a genuine cross-session collision yields >1 match and is
    skipped (fail safe)."""
    tm = _TASK_TOKEN_RE.search(stem)
    rm = _RUN_PROTOCOL_RE.search(stem)
    return (tm.group(1) if tm else None,
            f"{int(rm.group(1)):02d}" if rm else None)


_PHYSIO_STEM_RE = re.compile(
    r"^\d+_(?P<proto>.+)_recording-(?P<label>[A-Za-z0-9]+)_physio$")


def _rescue_unknown_physio(unknown: Path, bids_root: Path,
                           prov_rows: list, strict: bool) -> int:
    """Rescue physio recordings stranded in Unknown/.

    When a BOLD's ProtocolName fails reproin parse it lands in Unknown/, and
    dcm2niix writes its physio (`_recording-LABEL_physio.{tsv.gz,json}`) right
    next to it there rather than under derivatives/scanner/ — so the normal
    `_rescue_physio_recordings` pass never sees it.

    The physio is paired to its BOLD by (task, run) parsed from the filename
    (session is deliberately excluded — see `_physio_match_key`); it adopts that
    bold's full stem (sub/ses/task/acq/dir/run) with `_bold` ->
    `_recording-LABEL_physio` and any `_echo-N` dropped (physio is shared across
    echoes). The move is fail-closed via `_move_stem_files` (both companions move
    or neither; honours `strict` on OSError — but a *curated-destination*
    collision is treated as a benign skip, not a strict failure, since the
    curated file is left intact).

    Owning-subject resolution: the physio sidecar carries no PatientID and an
    ambiguous SeriesNumber, so the subject is resolved from provenance and the
    rescue is FAIL-CLOSED — it pairs only when the physio's protocol maps to
    exactly ONE subject, then confines the BOLD search to that subject's tree
    (which also excludes the root-level derivatives/scanner/ scouts). The mapping
    keys on the sanitized `OutputStem` (the on-disk `Unknown/<SN>_<proto>`
    spelling), NOT the raw `ProtocolName` column — the two diverge when the
    protocol has spaces/slashes/colons that the C side rewrites to `_` in the
    filename but not in the TSV's ProtocolName. An ambiguous (multi-subject
    same-protocol) or unresolved subject, or a non-unique BOLD match, leaves the
    physio in Unknown/ for the .bidsignore sweep (fail safe, no mis-pair).
    Returns the recordings moved."""
    stems: set[str] = set()
    for p in unknown.glob("*_recording-*_physio.tsv.gz"):
        stems.add(p.name[:-len(".tsv.gz")])
    for p in unknown.glob("*_recording-*_physio.json"):
        stems.add(p.name[:-len(".json")])
    if not stems:
        return 0
    # sanitized-protocol -> {subject tokens}, from the provenance OutputStem
    # (`[<dir>/]<SN>_<sanitized-proto>`); see docstring for why not ProtocolName.
    sani_proto_subjects: dict[str, set] = {}
    for r in prov_rows or []:
        base = str(r.get("OutputStem", "")).replace("\\", "/").rsplit("/", 1)[-1]
        pm = re.match(r"^\d+_(.+)$", base)
        if not pm:
            continue
        subj = _heudiconv_subject_token(str(r.get("PatientID", "")))
        if subj:
            sani_proto_subjects.setdefault(pm.group(1), set()).add(subj)
    moved = 0
    for stem in sorted(stems):
        m = _PHYSIO_STEM_RE.match(stem)
        if not m:
            continue
        key = _physio_match_key(stem)
        # Fail-closed on the owning subject: pair only when the provenance maps
        # the physio's (sanitized) protocol to exactly ONE subject, then scope
        # the BOLD search to that subject's tree (which also excludes the
        # root-level derivatives/scanner/ scouts). An ambiguous (multi-subject
        # same-protocol) or unresolved subject is left in Unknown/ rather than
        # risk a cross-subject mis-pair on a rerun or hand-curated root.
        subj_set = sani_proto_subjects.get(m.group("proto"))
        if not subj_set or len(subj_set) != 1:
            continue
        scope = bids_root / f"sub-{next(iter(subj_set))}"
        if not scope.is_dir():
            continue
        bases: set = set()
        for nii in (list(scope.rglob("*_bold.nii.gz")) +
                    list(scope.rglob("*_bold.nii"))):
            bstem = (nii.name[:-len(".nii.gz")]
                     if nii.name.endswith(".nii.gz") else nii.stem)
            if _physio_match_key(bstem) != key:
                continue
            base = _ECHO_TOKEN_RE.sub("", bstem)
            if base.endswith("_bold"):
                base = base[:-len("_bold")]
            bases.add((str(nii.parent), base))
        if len(bases) != 1:
            continue  # no unique parent BOLD — leave in Unknown/ (fail safe)
        parent_str, base = next(iter(bases))
        dst_base = f"{base}_recording-{m.group('label')}_physio"
        try:
            if _move_stem_files(unknown / stem, Path(parent_str) / dst_base) > 0:
                moved += 1
        except FileExistsError:
            continue  # curated file already present — non-destructive
        except OSError:
            if strict:
                raise
    return moved

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


_BIDS_EXTS = (".nii.gz", ".nii", ".json", ".bval", ".bvec", ".tsv", ".tsv.gz")


def _stem_has_files(stem: Path) -> bool:
    """True when any recognised BIDS file exists for a stem."""
    return any((stem.parent / f"{stem.name}{ext}").is_file() for ext in _BIDS_EXTS)


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
    #
    # Filter rows to those belonging to THIS subject (OutputStem contains
    # `sub-<name>/`). Without this, a `_ses-X` marker on subject A's scout
    # would propagate every subject in the BIDS root into `ses-X/`, and two
    # subjects with different sessions would block propagation entirely.
    sessions: set[str] = set()
    prov_rows = _load_provenance(sub_dir.parent)
    if prov_rows:
        sub_token = f"/{sub_dir.name}/"
        own_rows = [r for r in prov_rows
                    if sub_token in f"/{str(r.get('OutputStem', ''))}"]
        current_rows = [r for r in own_rows
                        if _stem_has_files(sub_dir.parent / str(r.get("OutputStem", "")))]
        study_uids = {str(r.get("StudyInstanceUID", ""))
                      for r in current_rows if r.get("StudyInstanceUID")}
        if study_uids:
            scoped_rows = [r for r in own_rows
                           if str(r.get("StudyInstanceUID", "")) in study_uids]
            ses_tsv = _session_from_provenance(scoped_rows)
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
    # Final fallback: the Unknown-rescue may have placed most of a study under
    # one timestamp `ses-<...>/` subtree while a bare-datatype series that DID
    # parse as ReproIn (e.g. a protocol of just "fmap") was written sessionless
    # at the subject root. A BIDS subject must not mix sessioned and sessionless
    # data, so when exactly one `ses-*/` subtree already exists and sessionless
    # datatype dirs remain, adopt that session and consolidate the strays into
    # it. Conservative: only fires for the single-existing-session case.
    if not sessions:
        children = [d for d in sub_dir.iterdir() if d.is_dir()]
        existing_ses = [d for d in children if d.name.startswith("ses-")]
        # Only a recognised BIDS datatype dir (anat/func/dwi/fmap/…) counts as
        # sessionless content to consolidate — never a hand-curated folder like
        # `code/` or `stimuli/` (audit 2026-06-20 cycle-3 F5).
        has_sessionless = any(d.name in _BIDS_DATATYPES for d in children)
        if len(existing_ses) == 1 and has_sessionless:
            sessions.add(existing_ses[0].name[len("ses-"):])
    if len(sessions) != 1:
        return None
    ses = next(iter(sessions))
    target_root = sub_dir / f"ses-{ses}"
    # Per-series atomicity: group files by stem-up-to-extension(s), preflight
    # every target, and skip the whole stem on any collision so a .nii.gz
    # and its .json never end up in different layouts. Only recognised BIDS
    # datatype dirs are moved — hand-curated subject-level folders are left in
    # place (audit 2026-06-20 cycle-3 F5).
    for datatype_dir in sorted(p for p in sub_dir.iterdir() if p.is_dir()):
        if datatype_dir.name.startswith("ses-") or datatype_dir.name not in _BIDS_DATATYPES:
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


def _heudiconv_subject_token(patient_id: str) -> str:
    """Mirror reproinFixupSubjectId in console/reproin.cpp: lowercase, strip
    '-'/'_', then keep alphanumerics only. Empty input → ""."""
    s = (patient_id or "").lower()
    s = "".join(c for c in s if c not in "-_")
    s = "".join(c for c in s if c.isalnum())
    return s


def _session_token_from_studydatetime(study_date: str, study_time: str) -> str:
    """Mirror the C-side %h fallback: "YYYYMMDDTHHMMSS". studyTime is the raw
    DICOM "HHMMSS.fff" string; trim to first 6 chars. Empty/invalid → "".

    Strict DICOM VR validation: StudyDate VR=DA is exactly 8 digits, StudyTime
    VR=TM starts with 6 digits. Rejecting non-digit input here blocks path
    traversal (e.g. a malicious DICOM with StudyDate="../../etc" would
    otherwise reach Path.rename via the rescue caller). The C-side
    reproinTsvField strips only \\t\\r\\n from these fields when writing the
    provenance TSV, so this Python validator is the privacy/safety boundary
    on the consumption side."""
    sd = (study_date or "").strip()
    st = (study_time or "").strip()
    if len(sd) < 8 or not sd[:8].isdigit():
        return ""
    if len(st) < 6 or not st[:6].isdigit():
        return ""
    return f"{sd[:8]}T{st[:6]}"


# Known BIDS top-level datatype directories. Restricting datatype to this set
# blocks path traversal via a malicious BidsGuess[0] (e.g. "../etc") that
# would otherwise become a directory component below the BIDS root.
# Audit M1: "mrs" must be present so MR Spectroscopy files that somehow fall
# into Unknown/ (rather than going through saveDcm2NiiMRS) can be rescued.
# The C side emits BidsGuess ["mrs","_svs"]; the rescue allowlist has to
# match. Keep this set aligned with the C-side bidsDataType emissions —
# WITH ONE DELIBERATE EXCEPTION: "ct" is intentionally OMITTED here even
# though the C side now emits BidsGuess ["ct","_ct"]. BEP-024 is unratified
# (the BIDS validator has no "ct" datatype yet), so under -f %H we want CT
# to stay parked in Unknown/ and be routed by the .bidsignore sweep rather
# than promoted into a ct/ directory. Do NOT add "ct" in a future
# "alignment" cleanup — that would change -f %H behavior. Revisit only when
# BEP-024 is stable in the validator.
_BIDS_DATATYPES = frozenset({
    "anat", "func", "dwi", "fmap", "perf", "pet", "mrs", "meg", "eeg",
    "ieeg", "beh", "micr", "nirs", "motion",
})

# BIDS entity-suffix charset: alphanumerics, hyphens, and underscores only.
# `_acq-X_dir-AP_run-1_bold` is the canonical shape; anything outside this
# alphabet (slashes, dots, control chars, NUL, ...) is rejected before the
# stem is concatenated into a path. `+` (not `*`) so an empty suffix is
# also rejected — an empty BidsGuess[1] would otherwise produce a malformed
# `<datatype>/sub-X_ses-Y` filename with no suffix word.
_BIDS_ENTITY_SUFFIX_RE = re.compile(r"\A[A-Za-z0-9_-]+\Z")

# OutputStem prefix that means the one-pass ReproIn writer successfully
# parsed the protocol. Rows under `Unknown/` failed reproin; rows under
# `derivatives/scanner/` are silent (scouts / DERIVED-flagged data).
_REPROIN_SUCCESS_PREFIX = "sub-"


def _maybe_collapse_nonreproin_root(out_root: Path, strict: bool) -> bool:
    """Collapse a single redundant `<StudyDescription>` hierarchy into
    `out_root` when the provenance shows ZERO ReproIn-parsed series.

    Background: dcm2niix `-f %H` writes to `<out>/<StudyDescription>/...`,
    and reproinPathify in console/reproin.cpp splits spaces and
    underscores in StudyDescription into path separators. So a real
    reproin user with StudyDescription = "Smith_Aging" gets the useful
    `<out>/Smith/Aging/sub-X/...` two-level grouping (mirroring the
    heudiconv reproin `<locator>/<study>` convention seen in
    /Users/chris/src/reproin/reproout_*). But a non-reproin user whose
    StudyDescription is just a descriptive label (or worse, a
    space-separated duplicate like "Studyname Studyname") gets
    `<out>/Studyname/Studyname/sub-X/` — content-free filler.

    Detection: every row in `.reproin_provenance.tsv` whose `OutputStem`
    starts with `Unknown/` means that series did NOT parse as ReproIn.
    If every row is Unknown/, no series benefited from the hierarchy,
    and we can move the study contents up to `out_root`. Conservative:
    only collapses when exactly one provenance TSV exists below
    out_root (multi-study trees are left alone), and only when the
    chain from out_root down to the study root is single-child at every
    level (so we never clobber sibling content with the rename).

    Returns True on collapse, False on any reason to skip (multi-study,
    partial reproin, sibling content present, target-name collision in
    out_root, no provenance at all).
    """
    tsvs = list(out_root.rglob(_PROVENANCE_TSV))
    if len(tsvs) != 1:
        return False
    study_root = tsvs[0].parent
    if study_root == out_root:
        return False  # already at the BIDS root; nothing to collapse
    # Walk back up from study_root to out_root, verifying each
    # intermediate is single-child. Bail if anything else lives at
    # any level (would clobber on the move).
    chain: list[Path] = []
    cursor = study_root
    while cursor != out_root:
        chain.append(cursor)
        parent = cursor.parent
        if parent == cursor:
            return False  # walked off the top without hitting out_root
        siblings = list(parent.iterdir())
        if len(siblings) != 1 or siblings[0] != cursor:
            return False  # other content at this level — refuse to clobber
        cursor = parent
    rows = _load_provenance(study_root)
    if not rows:
        return False
    # Reproin-success indicator: an OutputStem starting with "sub-" (the
    # one-pass ReproIn writer produces sub-X/ses-Y/<datatype>/...). Rows
    # under "Unknown/" mean reproin failed for that series, and rows
    # under "derivatives/" mean dcm2niix routed scouts/DERIVED-flagged
    # data to derivatives/scanner/ regardless of reproin parsing — both
    # are silent about reproin discipline, so neither blocks the collapse.
    for r in rows:
        if str(r.get("OutputStem", "")).startswith(_REPROIN_SUCCESS_PREFIX):
            return False  # at least one series parsed as ReproIn; keep hierarchy
    # Pre-flight: refuse if any study_root child name collides with an
    # existing entry in out_root (shouldn't happen given single-child
    # chain check, but defence in depth).
    children = list(study_root.iterdir())
    for child in children:
        if (out_root / child.name).exists():
            return False
    try:
        for child in children:
            shutil.move(str(child), str(out_root / child.name))
        # Iterate chain child-first so each rmdir sees an empty target.
        for d in chain:
            try:
                d.rmdir()
            except OSError:
                pass  # best-effort cleanup; partial chains are harmless
    except OSError as e:
        print(f"reproinx: study-root collapse mid-move failed: {e}",
              file=sys.stderr)
        if strict:
            raise
        return False
    return True


def _purge_all_discard_unknown(bids_root: Path, strict: bool) -> bool:
    """Delete `<bids_root>/Unknown/` when every JSON inside has
    `BidsGuess[0] == "discard"` AND every non-JSON file has a matching
    JSON sidecar (no orphans). Runs after the Unknown/-rescue, so files
    the rescue couldn't promote (CT, missing provenance, malformed
    BidsGuess) preserve the folder. Returns True on delete."""
    unknown = bids_root / "Unknown"
    if not unknown.is_dir():
        return False
    json_files = sorted(unknown.glob("*.json"))
    if not json_files:
        return False
    discard_stems: set[str] = set()
    for jp in json_files:
        try:
            data = _load_json(jp)
        except (OSError, ValueError, json.JSONDecodeError):
            return False
        guess = data.get("BidsGuess")
        if not (isinstance(guess, list) and guess and
                str(guess[0]).strip().lower() == "discard"):
            return False
        discard_stems.add(jp.name[:-len(".json")])
    # Every non-JSON file must have a discard-classified sidecar AND there
    # must be no subdirectories (which would be deleted blindly by rmtree).
    # Audit H1: the previous code looked at top-level files only and then
    # rmtree'd the whole tree — a nested dir slipped in by another tool
    # or by a prior crash would be silently lost.
    for p in unknown.iterdir():
        if p.is_dir():
            return False  # refuse to delete an unrecognised nested tree
        if not p.is_file() or p.suffix.lower() == ".json":
            continue
        stem = p.name
        if stem.endswith(".nii.gz"):
            stem = stem[:-len(".nii.gz")]
        else:
            stem = stem[:-len(p.suffix)] if p.suffix else stem
        if stem not in discard_stems:
            return False
    try:
        shutil.rmtree(unknown)
    except OSError as e:
        print(f"reproinx: all-discard Unknown/ purge failed for {bids_root}: {e}",
              file=sys.stderr)
        if strict:
            raise
        return False
    return True


def _rescue_unknown_dir(bids_root: Path, strict: bool) -> int:
    """Move files out of `<bids_root>/Unknown/` into proper BIDS layout using
    the JSON sidecar's `BidsGuess` field plus `PatientID`/`StudyDate`/
    `StudyTime` from `.reproin_provenance.tsv`. Series whose ProtocolName
    didn't parse as ReproIn land in `Unknown/` from the C side; this rescue
    promotes them to `sub-<patientId>/ses-<YYYYMMDDTHHMMSS>/<datatype>/`
    using the BIDS dataType + entity suffix dcm2niix already guessed.

    Per-series independent: each Unknown/ file is rescued on its own. No-op
    on the typical ReproIn-only conversion (no Unknown/ dir, returns 0).

    Skip rules (file stays in Unknown/):
    - JSON missing or unreadable.
    - `BidsGuess` absent or not a 2-element list.
    - `BidsGuess[0]` is "discard" or "derived" (the C side already routed
      these correctly; we shouldn't promote a scout into the BIDS root).
    - Provenance row missing for (StudyInstanceUID, SeriesNumber).
    - `PatientID`, `StudyDate`, or `StudyTime` empty (e.g. -ba y mode).

    Collision policy: if the target stem already exists, append `_run-NN`
    (heudiconv-style, zero-padded) before the entity suffix.

    Returns the number of file-stems rescued."""
    unknown = bids_root / "Unknown"
    if not unknown.is_dir():
        return 0
    rows = _load_provenance(bids_root)
    if not rows:
        return 0
    prov_idx: dict[tuple[str, int], dict[str, object]] = {}
    for r in rows:
        suid = str(r.get("StudyInstanceUID", ""))
        try:
            sn = int(str(r.get("SeriesNumber", "0")))
        except (TypeError, ValueError):
            sn = 0
        prov_idx[(suid, sn)] = r

    # Study-scoped session recovery. A protocol may carry an explicit reproin
    # `ses-<X>` token (e.g. "ses-pre_task-bernd_bold"). Because heudiconv (and
    # the C parser mirroring it) only honour `ses-` after a leading <datatype>
    # token, these session-first protocols fail to parse and the session would
    # otherwise be lost to the StudyDate/Time timestamp fallback below. Recover
    # it here: for each StudyInstanceUID collect the `ses-<X>` token from every
    # series' ProtocolName/SeriesDescription; when the study agrees on exactly
    # one label, use it for every rescued series in that study. A study with no
    # `ses-` token (or conflicting ones) falls back to the timestamp session,
    # preserving multi-study-per-patient disambiguation.
    study_session: dict[str, str] = {}
    study_session_blocked: set[str] = set()
    for r in rows:
        suid = str(r.get("StudyInstanceUID", ""))
        if not suid or suid in study_session_blocked:
            continue
        label = ""
        for fld in ("ProtocolName", "SeriesDescription"):
            m = _SES_PROTOCOL_RE.search(str(r.get(fld, "")))
            if m:
                label = re.sub(r"[^A-Za-z0-9]", "", m.group(1))
                break
        if not label:
            continue
        prev = study_session.get(suid)
        if prev is None:
            study_session[suid] = label
        elif prev != label:
            study_session.pop(suid, None)
            study_session_blocked.add(suid)

    # Two-pass design (revised 2026-06-08). The previous one-pass scheme
    # iterated `sorted(unknown.glob("*.json"))` (alphabetical by filename)
    # and gave the first file at a stem the un-numbered name, with later
    # collisions getting `_run-01`, `_run-02`, ... The result was order-
    # dependent on string-sort of the dcm2niix series-prefixed filenames:
    # works for series 30 vs 31 ("30..." < "31..."), breaks for any
    # cross-decade pair (e.g. series 2 vs 10 → "10..." < "2..."). It also
    # produced asymmetric output (one file `_svs`, sibling `_run-01_svs`)
    # which downstream tooling has to special-case.
    #
    # New scheme:
    #   Pass 1 — for every json in Unknown/, compute (target_dir,
    #            base_stem, run_template, src_stem_name, SeriesNumber).
    #            Skip-on-validation-error stays as before.
    #   Pass 2 — group by (target_dir, base_stem). Single-member groups
    #            land at base_stem (un-numbered, identical to before).
    #            Multi-member groups land at `_run-NN` for every member,
    #            with NN assigned by ascending SeriesNumber (scanner
    #            acquisition order, deterministic and reproducible). The
    #            "implicit run-zero" sibling at base_stem is no longer
    #            emitted — heudiconv-compatible.
    #
    # An existing on-disk family at base_stem (from a prior run, or from a
    # C-side direct write) is treated as another member of the group
    # WITHOUT renaming it, so re-runs don't shuffle previously-numbered
    # output. The new rescues then get `_run-NN` starting at the lowest
    # free index. This is the safe choice; perfect re-numbering would
    # require touching pre-existing files which has its own surprises.
    rescued = 0
    derived_moved = 0  # derived/discard families relocated to derivatives/scanner/
    task_re = re.compile(r"task-([A-Za-z0-9]+)")
    candidates: list[dict] = []  # one entry per rescuable json
    for jp in sorted(unknown.glob("*.json")):
        try:
            data = _load_json(jp)
        except (OSError, ValueError, json.JSONDecodeError):
            continue
        guess = data.get("BidsGuess")
        if not isinstance(guess, list) or len(guess) < 2:
            continue
        datatype = str(guess[0]).strip()
        entity_suffix = str(guess[1])  # leading "_" is included by C side
        if datatype.lower() in ("discard", "derived"):
            # Known-but-not-raw data (scanner-derived DWI maps, scouts, ...).
            # It is NOT raw BIDS, so route it to derivatives/scanner/ — dropped
            # by default, kept under --keep-derivatives — instead of leaving it
            # in Unknown/ (where the .bidsignore sweep would keep it in the raw
            # tree). Mirrors how the C side routes parsed derived/discard series.
            # https://bids-specification.readthedocs.io/en/stable/derivatives/introduction.html
            stem = jp.name[:-len(".json")]
            dest = bids_root / "derivatives" / "scanner" / "Unknown"
            try:
                dest.mkdir(parents=True, exist_ok=True)
                _move_stem_files(unknown / stem, dest / stem)
                derived_moved += 1
            except FileExistsError:
                pass  # curated dest already present — leave in Unknown/ (benign)
            except OSError:
                if strict:
                    raise
                pass  # leave in Unknown/ for the .bidsignore sweep (fail safe)
            continue
        if datatype.lower() == "":
            continue
        # Path-safety validation. BidsGuess is written by the C side from
        # parsed DICOM strings; both fields flow into a Path component
        # below, so reject anything outside the BIDS charset BEFORE the
        # rename. datatype must be a known BIDS top-level directory;
        # entity_suffix must be alphanumeric + '-' + '_'. Either failure
        # leaves the file in Unknown/ where _bidsguess_unknown_leftover_files
        # will route it to .bidsignore — no data loss, no traversal.
        if datatype.lower() not in _BIDS_DATATYPES:
            continue
        if not _BIDS_ENTITY_SUFFIX_RE.fullmatch(entity_suffix):
            continue
        # func/_bold and func/_sbref REQUIRE _task-X_ per BIDS spec. The C
        # BidsGuess heuristic does not extract task from ProtocolName, so
        # we recover it here: prefer ProtocolName, fall back to
        # SeriesDescription, default to "rest" (matches the C %h
        # createDummyBidsBoilerplate fallback).
        if datatype.lower() == "func" and "_task-" not in entity_suffix:
            task = ""
            for src in (str(data.get("ProtocolName", "")),
                        str(data.get("SeriesDescription", ""))):
                m = task_re.search(src)
                if m:
                    task = m.group(1)
                    break
            if not task:
                task = "rest"
            entity_suffix = f"_task-{task}{entity_suffix}"
        # Run-entity normalisation. dcm2niix's BidsGuess seeds `_run-<SeriesNumber>`
        # (a raw acquisition counter, not a BIDS run index). Drop it. When the
        # reproin protocol explicitly states a numeric run, re-inject it in
        # canonical BIDS position; otherwise leave the series run-less and let
        # the Pass-2 collision numbering add a run only for genuine duplicates.
        entity_suffix = _RUN_ENTITY_RE.sub("", entity_suffix)
        prot_run = ""
        for src in (str(data.get("ProtocolName", "")),
                    str(data.get("SeriesDescription", ""))):
            m = _RUN_PROTOCOL_RE.search(src)
            if m:
                prot_run = m.group(1)
                break
        if prot_run:
            entity_suffix = _insert_run_entity(
                entity_suffix, f"run-{int(prot_run):02d}")
        suid = str(data.get("StudyInstanceUID", ""))
        try:
            sn = int(data.get("SeriesNumber", 0))
        except (TypeError, ValueError):
            sn = 0
        prov_row = prov_idx.get((suid, sn))
        if prov_row is None:
            continue
        subj = _heudiconv_subject_token(str(prov_row.get("PatientID", "")))
        sess = study_session.get(suid, "")
        if not sess:
            sess = _session_token_from_studydatetime(
                str(prov_row.get("StudyDate", "")),
                str(prov_row.get("StudyTime", "")))
        if not subj or not sess:
            continue
        sub_token = f"sub-{subj}"
        ses_token = f"ses-{sess}"
        target_dir = bids_root / sub_token / ses_token / datatype
        # _run-NN goes at its canonical BIDS position (before part/echo/... and
        # the suffix word). Only used by Pass 2 when a stem actually collides.
        run_body = _insert_run_entity(entity_suffix, "run-{idx:02d}")
        run_template = f"{sub_token}_{ses_token}{run_body}"
        base_stem = f"{sub_token}_{ses_token}{entity_suffix}"
        src_stem_name = jp.name[:-len(".json")]
        candidates.append({
            "target_dir": target_dir,
            "base_stem": base_stem,
            "run_template": run_template,
            "src_stem_name": src_stem_name,
            "series_number": sn,
            "json_name": jp.name,
        })

    # Pass 2: group by (target_dir, base_stem), assign final names, move.
    def _family_exists(target_dir: Path, stem: str) -> bool:
        return any((target_dir / f"{stem}{ext}").exists()
                   for ext in (".nii.gz", ".nii", ".json", ".bvec", ".bval"))

    groups: dict[tuple[str, str], list[dict]] = {}
    for c in candidates:
        key = (str(c["target_dir"]), c["base_stem"])
        groups.setdefault(key, []).append(c)

    for (target_dir_str, base_stem), members in groups.items():
        target_dir = Path(target_dir_str)
        existing_at_base = _family_exists(target_dir, base_stem)
        if len(members) == 1 and not existing_at_base:
            # Single source for this stem AND no on-disk collision —
            # un-numbered name, exactly as before.
            members[0]["chosen"] = base_stem
        else:
            # Multi-member group, OR single-member colliding with an
            # existing on-disk family. Sort members by SeriesNumber
            # ascending (scanner acquisition order) and assign run-NN
            # starting at the lowest free index. The pre-existing file
            # at base_stem (if any) is left untouched — see policy note
            # above.
            members.sort(key=lambda c: c["series_number"])
            # Find run indices not already occupied on disk.
            taken = set()
            for idx in range(1, 100):
                candidate = members[0]["run_template"].format(idx=idx)
                if _family_exists(target_dir, candidate):
                    taken.add(idx)
            free_iter = (i for i in range(1, 100) if i not in taken)
            ok = True
            for m in members:
                try:
                    nxt = next(free_iter)
                except StopIteration:
                    ok = False
                    break
                m["chosen"] = m["run_template"].format(idx=nxt)
            if not ok:
                # >99 collisions — pathological. Leave members in
                # Unknown/ and let the .bidsignore sweep route them.
                for m in members:
                    m["chosen"] = None

    for c in candidates:
        chosen = c.get("chosen")
        if not chosen:
            continue
        # Move the whole family through the all-or-nothing primitive: it
        # preflights every companion (so a target created after planning is
        # detected, not overwritten) and rolls back on a mid-family failure
        # (audit 2026-06-20 cycle-3 F4 — was a per-extension rename loop).
        try:
            if _move_stem_files(unknown / c["src_stem_name"],
                                c["target_dir"] / chosen) > 0:
                rescued += 1
        except FileExistsError:
            print(f"reproinx: Unknown/-rescue collision — kept {c['json_name']} "
                  f"(target {chosen} already present)", file=sys.stderr)
            if strict:
                raise
        except OSError as e:
            print(f"reproinx: Unknown/-rescue failed for {c['json_name']}: {e}",
                  file=sys.stderr)
            if strict:
                raise
    # Physio recordings stranded in Unknown/ (parent BOLD failed reproin parse):
    # pair each to its rescued BOLD and move into that bold's func/ dir. Runs
    # after the BOLD moves above so the parents are already in place.
    rescued += _rescue_unknown_physio(unknown, bids_root, rows, strict)
    if derived_moved > 0:
        print(f"  {bids_root}: moved {derived_moved} derived/discard stem(s) "
              "from Unknown/ to derivatives/scanner/", file=sys.stderr)
    # If Unknown/ is empty after rescue, prune it.
    try:
        if unknown.is_dir() and not any(unknown.iterdir()):
            unknown.rmdir()
    except OSError:
        pass
    return rescued


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
    # appears in the TSV, mark this stem as a collision sibling of `base` —
    # but only when both rows refer to the SAME source series (matching
    # StudyInstanceUID + ProtocolName + SeriesDescription). Without this
    # guard, legitimate labels ending in a single lowercase letter (e.g. an
    # `acq-` value that happens to be 1 char) get falsely grouped with a
    # different series whose stem happens to be one char shorter.
    def _same_series(a: dict, b: dict) -> bool:
        for k in ("StudyInstanceUID", "ProtocolName", "SeriesDescription"):
            if str(a.get(k, "")) != str(b.get(k, "")):
                return False
        return True
    groups: dict[str, list[dict[str, object]]] = {}
    for stem in by_stem:
        if len(stem) < 2:
            continue
        last = stem[-1]
        base = stem[:-1]
        if "a" <= last <= "z" and base in by_stem and _same_series(by_stem[stem], by_stem[base]):
            groups.setdefault(base, []).append(by_stem[stem])
    # Include the base row in each group.
    for base in list(groups.keys()):
        groups[base].append(by_stem[base])

    renamed = 0
    for base, members in groups.items():
        ordered = sorted(members,
                         key=lambda r: int(str(r.get("SeriesNumber", "0")) or "0"))
        targets = [
            base if idx == 0 else f"{base}__dup-{idx:02d}"
            for idx, _row in enumerate(ordered)
        ]
        if all(_stem_has_files(bids_root / target) for target in targets):
            continue
        # Build (current_stem, target_stem) renames.
        moves: list[tuple[str, str]] = []
        for idx, row in enumerate(ordered):
            current = str(row.get("OutputStem", ""))
            target = base if idx == 0 else f"{base}__dup-{idx:02d}"
            if current != target:
                moves.append((current, target))
        if not moves:
            continue
        # If NONE of the group's as-written members (including the unsuffixed
        # base, which `moves` omits) still exist, an earlier pass (e.g.
        # _resolve_part_entities renaming magnitude/phase members) fully resolved
        # this group — skip silently. A partially-resolved group (some members
        # still present) falls through to the stale/partial warning below.
        if not any(_stem_has_files(bids_root / str(row.get("OutputStem", "")))
                   for row in ordered):
            continue
        current_stems = {current for current, _target in moves}
        unsafe = False
        for current, target in moves:
            if not _stem_has_files(bids_root / current):
                unsafe = True
                break
            if _stem_has_files(bids_root / target) and target not in current_stems:
                unsafe = True
                break
        # A member already at its target (the unsuffixed base, omitted from
        # `moves`) must still have files; if the base is gone while a suffixed
        # sibling survives, renaming would leave a dup-only family. Validate it.
        if not unsafe:
            for idx, row in enumerate(ordered):
                current = str(row.get("OutputStem", ""))
                target = base if idx == 0 else f"{base}__dup-{idx:02d}"
                if current == target and not _stem_has_files(bids_root / target):
                    unsafe = True
                    break
        if unsafe:
            print(f"reproinx: __dup rename skipped stale or partial group '{base}'",
                  file=sys.stderr)
            continue
        # Two-phase rename: stems first → tmp; tmp → target. Prevents an
        # ordering-dependent collision when the renaming would overwrite a
        # not-yet-moved sibling (e.g. lowest SeriesNumber currently at "_a").
        staged: list[tuple[str, str, str]] = []
        finalized: list[tuple[str, str, str]] = []
        try:
            for i, (current, target) in enumerate(moves):
                tmp = f"{base}__reproinx-tmp-{i:03d}"
                if _move_stem_files(bids_root / current, bids_root / tmp) < 1:
                    raise OSError(f"source family disappeared: {current}")
                staged.append((current, tmp, target))
            for current, tmp, target in staged:
                if _move_stem_files(bids_root / tmp, bids_root / target) < 1:
                    raise OSError(f"temporary family disappeared: {tmp}")
                finalized.append((current, tmp, target))
        except Exception:
            # Reverse phase 2 first so every staged name is free, then phase 1.
            for _current, tmp, target in reversed(finalized):
                try:
                    _move_stem_files(bids_root / target, bids_root / tmp)
                except Exception:
                    pass
            for current, tmp, _target in reversed(staged):
                if not _stem_has_files(bids_root / tmp):
                    continue
                try:
                    _move_stem_files(bids_root / tmp, bids_root / current)
                except Exception:
                    pass
            raise
        renamed += len(finalized)
    return renamed


def _move_stem_files(src_stem: Path, dst_stem: Path) -> int:
    """Move all `<src_stem><ext>` files to `<dst_stem><ext>` atomically.

    Operates on the recognised BIDS extensions (.nii.gz, .json, .bval, …).
    Returns the number of files moved. Skips when source missing.

    All-or-nothing: every destination in the family is preflighted BEFORE any
    rename, so a collision on (say) the `.json` cannot leave the `.nii.gz`
    already moved and the family split. Raises `FileExistsError` if any target
    exists; nothing is moved in that case. If a later `os.replace` fails mid-
    family (e.g. ENOSPC/EPERM) the moves already done are rolled back (best
    effort — a rollback `os.replace` that itself fails is swallowed) before the
    `OSError` propagates, so the family is normally not left split."""
    src_dir = src_stem.parent
    src_name = src_stem.name
    if not src_dir.is_dir():
        return 0
    dst_dir = dst_stem.parent
    dst_name = dst_stem.name
    # Preflight: gather the present family and verify every target is free.
    pairs: list[tuple[Path, Path]] = []
    for ext in _BIDS_EXTS:
        src = src_dir / f"{src_name}{ext}"
        if not src.is_file():
            continue
        dst = dst_dir / f"{dst_name}{ext}"
        if dst.exists():
            raise FileExistsError(f"{dst} already exists")
        pairs.append((src, dst))
    if not pairs:
        return 0
    dst_dir.mkdir(parents=True, exist_ok=True)
    done: list[tuple[Path, Path]] = []
    for src, dst in pairs:
        try:
            os.replace(str(src), str(dst))
        except OSError:
            # Roll back the companions already moved so the family stays whole.
            for s, d in reversed(done):
                try:
                    os.replace(str(d), str(s))
                except OSError:
                    pass
            raise
        done.append((src, dst))
    return len(pairs)


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
#
# Text below is intentionally copied verbatim from heudiconv's reproin output
# (CHANGES, README, dataset_description.json, participants.json) so the
# resulting tree is byte-identical to `heudiconv -f reproin` at this layer.
# Treat these strings like reference data — keep them in sync with the upstream
# heuristic when its scaffolding changes, do not "polish" the TODO wording.

# No trailing newline — heudiconv writes these without one and we want byte
# identity for downstream tools that diff against the reference tree.
_CHANGES_TEMPLATE = (
    "0.0.1  Initial data acquired\n"
    "TODOs:\n"
    "\t- verify and possibly extend information in participants.tsv (see for example http://datasets.datalad.org/?dir=/openfmri/ds000208)\n"
    "\t- fill out dataset_description.json, README, sourcedata/README (if present)\n"
    "\t- provide _events.tsv file for each _bold.nii.gz with onsets of events (see  '8.5 Task events'  of BIDS specification)"
)
_README_TEMPLATE = (
    "TODO: Provide description for the dataset -- basic details about the study, possibly pointing to pre-registration (if public or embargoed)"
)
_DATASET_DESCRIPTION = {
    "Acknowledgements": "We thank Terry Sacket and the rest of the DBIC (Dartmouth Brain Imaging Center) personnel for assistance in data collection, and Yaroslav O. Halchenko for preparing BIDS dataset. TODO: adjust to your case.",
    "Authors": ["TODO:", "First1 Last1", "First2 Last2", "..."],
    "BIDSVersion": "1.8.0",
    "DatasetDOI": "TODO: eventually a DOI for the dataset",
    "Funding": ["TODO", "GRANT #1", "GRANT #2"],
    "HowToAcknowledge": "TODO: describe how to acknowledge -- either cite a corresponding paper, or just in acknowledgement section",
    "License": "TODO: choose a license, e.g. PDDL (http://opendatacommons.org/licenses/pddl/)",
    "Name": "TODO: name of the dataset",
    "ReferencesAndLinks": ["TODO", "List of papers or websites"],
}
_PARTICIPANTS_JSON = {
    "participant_id": {
        "Description": "Participant identifier",
    },
    "age": {
        "Description": "Age in years (TODO - verify) as in the initial session, might not be correct for other sessions",
    },
    "sex": {
        "Description": "self-rated by participant, M for male/F for female (TODO: verify)",
    },
    "group": {
        "Description": "(TODO: adjust - by default everyone is in control group)",
    },
}
_SCANS_JSON = {
    "filename": {
        "Description": "Name of the nifti file",
    },
    "acq_time": {
        "LongName": "Acquisition time",
        "Description": "Acquisition time of the particular scan",
    },
    "operator": {
        "Description": "Name of the operator",
    },
    "randstr": {
        "LongName": "Random string",
        "Description": "md5 hash of UIDs",
    },
}


def _write_text_if_absent(path: Path, content: str) -> None:
    if not path.exists():
        path.write_text(content)


_DCM2NIIX_DDESC_NAME = "dcm2niix dummy dataset"


_DDESC_SKIP_FILENAMES = {"dataset_description.json", "participants.json", "scans.json"}


def _discover_dcm2niix_version(bids_root: Path) -> Optional[str]:
    """Read `ConversionSoftwareVersion` from a raw dcm2niix sidecar in the
    BIDS tree. Returns None if no sidecar carries it (e.g. --no-convert
    run on an empty tree); callers should then omit the Version field
    rather than writing a literal 'unknown'. The search is sorted (so
    mixed-version reruns pick a deterministic winner) and skips
    `derivatives/` to avoid mistaking a downstream pipeline's sidecar for
    a dcm2niix conversion stamp."""
    candidates = []
    for jp in bids_root.rglob("*.json"):
        if jp.name in _DDESC_SKIP_FILENAMES:
            continue
        if "derivatives" in jp.parts:
            continue
        candidates.append(jp)
    candidates.sort()
    for jp in candidates:
        try:
            data = _load_json(jp)
        except (OSError, ValueError, json.JSONDecodeError):
            continue
        if not isinstance(data, dict):
            continue
        sw = data.get("ConversionSoftware")
        if isinstance(sw, str) and sw and sw != "dcm2niix":
            continue
        v = data.get("ConversionSoftwareVersion")
        if isinstance(v, str) and v.strip():
            return v.strip()
    return None


def _dcm2niix_generated_by(version: Optional[str]) -> dict:
    """Single GeneratedBy entry for dcm2niix. `version` should be the cached
    output of `_discover_dcm2niix_version`; the Version key is omitted when
    no sidecar carries `ConversionSoftwareVersion`."""
    entry = {
        "Name": "dcm2niix",
        "Description": "DICOM to NIfTI converter",
        "CodeURL": "https://github.com/rordenlab/dcm2niix",
    }
    if version is not None:
        entry["Version"] = version
    return entry


def _upgrade_dataset_description(bids_root: Path) -> None:
    """Replace dcm2niix's dummy `dataset_description.json` with the heudiconv
    reproin template, extended with the `GeneratedBy`, `SourceDatasets`, and
    `DatasetType` keys that the BIDS validator recommends but heudiconv's
    stock template omits. `DatasetType` is explicitly set to "raw": the
    validator (context.ts) infers "derivative" whenever `GeneratedBy` is
    present and `DatasetType` is absent, which forces dozens of
    derivative-mode rules (e.g. `SkullStripped` required on anat .nii.gz).
    Hand-edited files keep their curated values; we only *backfill* the
    recommended keys when missing. `_discover_dcm2niix_version` is invoked
    lazily so curated files that already carry the recommended keys do not
    pay a recursive scan of the BIDS tree.
    """
    p = bids_root / "dataset_description.json"
    cached_version: list = []  # 1-slot cache: lazy, computed at most once
    def gen_by() -> dict:
        if not cached_version:
            cached_version.append(_discover_dcm2niix_version(bids_root))
        return _dcm2niix_generated_by(cached_version[0])
    if not p.exists():
        desc = dict(_DATASET_DESCRIPTION)
        desc["DatasetType"] = "raw"
        desc["GeneratedBy"] = [gen_by()]
        desc["SourceDatasets"] = []
        _save_json(p, desc)
        return
    try:
        data = _load_json(p)
    except (OSError, ValueError, json.JSONDecodeError):
        return
    if not isinstance(data, dict):
        return
    if data.get("Name") == _DCM2NIIX_DDESC_NAME:
        desc = dict(_DATASET_DESCRIPTION)
        desc["DatasetType"] = "raw"
        desc["GeneratedBy"] = [gen_by()]
        desc["SourceDatasets"] = []
        _save_json(p, desc)
        return
    changed = False
    if "DatasetType" not in data:
        data["DatasetType"] = "raw"
        changed = True
    if "GeneratedBy" not in data:
        data["GeneratedBy"] = [gen_by()]
        changed = True
    if "SourceDatasets" not in data:
        data["SourceDatasets"] = []
        changed = True
    if changed:
        _save_json(p, data)


def _write_root_scaffolding(out_root: Path) -> None:
    """Write CHANGES, README (no .md), scans.json at the root.

    `.bidsignore` is intentionally NOT created here. heudiconv writes a
    literal `.duecredit.p` line so the BIDS validator ignores duecredit's
    citation pickle, but neither dcm2niix (C++) nor reproinx (stdlib
    Python) imports duecredit, so the file would never exist on this
    code path. The post-process passes (collision-suffix, single-vol DWI,
    bidsguess residuals) create `.bidsignore` on demand when there is
    something real to ignore."""
    _write_text_if_absent(out_root / "CHANGES", _CHANGES_TEMPLATE)
    _write_text_if_absent(out_root / "README", _README_TEMPLATE)
    _upgrade_dataset_description(out_root)
    scans_json = out_root / "scans.json"
    if not scans_json.exists():
        _save_json(scans_json, _SCANS_JSON)


_DICOM_AGE_RE = re.compile(r"^0*(\d+)Y$")


def _parse_age(raw: str) -> str:
    """Convert DICOM `(0010,1010) PatientAge` ('026Y') to a BIDS year integer.

    Returns 'n/a' when the unit is not years (D/W/M), the field is empty, or
    parsing fails. Mirrors heudiconv's coarse year-only convention.
    """
    if not raw:
        return "n/a"
    m = _DICOM_AGE_RE.match(raw.strip())
    return m.group(1) if m else "n/a"


def _demographics_from_provenance(bids_root: Path) -> dict[str, dict[str, str]]:
    """Return `{sub-<label>: {'age', 'sex', 'study_datetime'}}` from the C-side
    provenance TSV. Subject label is recovered from `OutputStem` (everything
    between the first `sub-` and the next path separator). `study_datetime`
    concatenates DICOM `StudyDate` + `StudyTime` for chronological ordering
    (heudiconv's participants.tsv ordering).
    """
    out: dict[str, dict[str, str]] = {}
    for row in _load_provenance(bids_root):
        stem = str(row.get("OutputStem", ""))
        m = re.search(r"(^|/)(sub-[^/]+)/", stem)
        if not m:
            continue
        sub = m.group(2)
        age = _parse_age(str(row.get("PatientAge", "")))
        sex_raw = str(row.get("PatientSex", "")).strip()
        sex = sex_raw if sex_raw in ("M", "F", "O") else "n/a"
        sd = str(row.get("StudyDate", "")).strip()
        st = str(row.get("StudyTime", "")).strip()
        sdt = (sd + st) if (sd or st) else ""
        # First non-trivial row per subject wins; later rows only fill in n/a.
        cur = out.setdefault(sub, {"age": "n/a", "sex": "n/a", "study_datetime": ""})
        if cur["age"] == "n/a" and age != "n/a":
            cur["age"] = age
        if cur["sex"] == "n/a" and sex != "n/a":
            cur["sex"] = sex
        if not cur["study_datetime"] and sdt:
            cur["study_datetime"] = sdt
    return out


def _write_participants(bids_root: Path) -> None:
    """Emit participants.tsv (one row per sub-* in THIS root) and .json.

    Columns mirror heudiconv reproin: `participant_id, age, sex, group`.
    `group` defaults to 'control' (matching reproin's TODO placeholder).
    `age` and `sex` are pulled from the C-side provenance TSV.

    On re-runs we merge new participant rows additively. Hand-edited rows
    (existing participant_ids) are left untouched even if the provenance
    would now provide age/sex — this preserves curated overrides.
    """
    sub_names = {s.name for s in bids_root.iterdir()
                 if s.is_dir() and s.name.startswith("sub-")
                 and not s.name.startswith("sub-.")}
    if not sub_names:
        return
    demos = _demographics_from_provenance(bids_root)
    # Order subjects chronologically by StudyDate (matches heudiconv's
    # participants.tsv). Subjects missing a date fall back to alphabetical
    # at the tail.
    def _order_key(s: str) -> tuple:
        sdt = demos.get(s, {}).get("study_datetime", "")
        return (sdt == "", sdt, s)
    subs = sorted(sub_names, key=_order_key)
    tsv = bids_root / "participants.tsv"
    header = "participant_id\tage\tsex\tgroup\n"
    def _row(sub: str) -> str:
        d = demos.get(sub, {"age": "n/a", "sex": "n/a"})
        return f"{sub}\t{d['age']}\t{d['sex']}\tcontrol\n"
    if tsv.exists():
        # newline="" disables universal-newline translation so CRLF survives
        # and `endswith("\r\n")` below works against the on-disk bytes. The
        # previous default `read_text()` silently rewrote CRLF -> LF and made
        # the line-end preservation branch unreachable.
        with tsv.open("r", newline="") as f:
            existing = f.read().splitlines(keepends=True)
        if not existing:
            with tsv.open("w") as f:
                f.write(header)
                for s in subs:
                    f.write(_row(s))
            existing = [header] + [_row(s) for s in subs]
        # Header mismatch (curated columns like `handedness`, `group=patient`,
        # CRLF line endings, etc.) means a hand-edited file — never overwrite.
        # Append only `participant_id`s that aren't already listed, leaving the
        # extra columns blank ("n/a") on the new rows. Preserves curated work
        # across re-runs.
        if existing and existing[0] != header:
            print(
                f"reproinx: {tsv} has a non-default header — preserving existing "
                f"columns and appending only new participant_ids",
                file=sys.stderr,
            )
            existing_header = existing[0].rstrip("\r\n")
            cols = existing_header.split("\t")
            seen = {ln.split("\t", 1)[0] for ln in existing[1:] if ln.strip()}
            new_rows = [s for s in subs if s not in seen]
            if new_rows:
                with tsv.open("a") as f:
                    line_end = "\r\n" if existing[0].endswith("\r\n") else "\n"
                    if existing and not existing[-1].endswith(("\n", "\r")):
                        f.write(line_end)
                    for s in new_rows:
                        d = demos.get(s, {"age": "n/a", "sex": "n/a"})
                        cells = [s]
                        for col in cols[1:]:
                            cells.append(d.get(col, "n/a") if col in ("age", "sex") else
                                         ("control" if col == "group" else "n/a"))
                        f.write("\t".join(cells) + line_end)
        else:
            seen = {ln.split("\t", 1)[0] for ln in existing[1:] if ln.strip()}
            new_rows = [s for s in subs if s not in seen]
            if new_rows:
                with tsv.open("a") as f:
                    for s in new_rows:
                        f.write(_row(s))
    else:
        with tsv.open("w") as f:
            f.write(header)
            for s in subs:
                f.write(_row(s))
    pj = bids_root / "participants.json"
    if not pj.exists():
        _save_json(pj, _PARTICIPANTS_JSON)


_TASK_STEM_RE = re.compile(r"(task-[A-Za-z0-9]+(?:_acq-[A-Za-z0-9]+)?)_")


def _json_sidecar_for_nii(nii: Path) -> Optional[Path]:
    name = nii.name
    if name.endswith(".nii.gz"):
        return nii.with_name(name[:-7] + ".json")
    if name.endswith(".nii"):
        return nii.with_name(name[:-4] + ".json")
    return None


def _ensure_taskname(json_path: Path, task_name: str) -> None:
    if not json_path.is_file():
        return
    try:
        data = _load_json(json_path)
    except (OSError, ValueError, json.JSONDecodeError):
        return
    if not isinstance(data, dict) or data.get("TaskName"):
        return
    data["TaskName"] = task_name
    _save_json(json_path, data)


def _backfill_tasknames(out_root: Path, glob: str) -> int:
    """Backfill `TaskName` (from the filename `task-` entity) into sidecars
    matching `glob`. The dataset-level `task-X_bold.json` TaskName is NOT
    inherited across other suffixes (`_physio`, `_sbref`), so the BIDS validator
    warns SIDECAR_KEY_RECOMMENDED(TaskName) on each. TaskName is the task label
    itself — runs/acqs of one task legitimately SHARE it (run-/acq- distinguish
    acquisitions, not the task). Skips sidecars that already define TaskName and
    any left under derivatives/. Returns the count updated."""
    n = 0
    for jp in out_root.rglob(glob):
        if any(part == "derivatives" for part in jp.parts):
            continue
        m = re.search(r"task-([A-Za-z0-9]+)", jp.name)
        if not m:
            continue
        try:
            data = _load_json(jp)
        except (OSError, ValueError, json.JSONDecodeError):
            continue
        if not isinstance(data, dict) or data.get("TaskName"):
            continue
        data["TaskName"] = m.group(1)
        _save_json(jp, data)
        n += 1
    return n


def _ensure_physio_tasknames(out_root: Path) -> int:
    return _backfill_tasknames(out_root, "*_physio.json")


def _ensure_sbref_tasknames(out_root: Path) -> int:
    return _backfill_tasknames(out_root, "*_sbref.json")


def _emit_task_bold_jsons(out_root: Path) -> None:
    """For every distinct `task-X[_acq-Y]_*_bold.nii.gz`, ensure a top-level
    `task-X[_acq-Y]_bold.json` exists. dcm2niix's default boilerplate emits
    `task-rest_bold.json` only; this catches per-acq variants."""
    stems: set[str] = set()
    tasks_with_acq: set[str] = set()
    for nii in out_root.rglob("*_bold.nii*"):
        if any(part == "derivatives" for part in nii.parts):
            continue
        m = _TASK_STEM_RE.search(nii.name)
        if m:
            stem = m.group(1)
            stems.add(stem)
            task_name = stem.split("_acq-")[0][len("task-"):]
            if "_acq-" in stem:
                tasks_with_acq.add(task_name)
            sidecar = _json_sidecar_for_nii(nii)
            if sidecar is not None:
                _ensure_taskname(sidecar, task_name)
    for stem in sorted(stems):
        task_name = stem.split("_acq-")[0][len("task-"):]
        if "_acq-" not in stem and task_name in tasks_with_acq:
            continue
        path = out_root / f"{stem}_bold.json"
        if path.exists():
            continue
        _save_json(path, {"TaskName": task_name})
    for task_name in sorted(tasks_with_acq):
        default = out_root / f"task-{task_name}_bold.json"
        if not default.exists():
            continue
        if _is_generated_task_stub(default, task_name):
            try:
                default.unlink()
            except OSError:
                pass
        else:
            print(f"reproinx: {default} conflicts with _acq- task sidecars; "
                  "preserving curated file", file=sys.stderr)
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


def _is_generated_task_stub(path: Path, task_name: str) -> bool:
    """True iff `path` is a generated root task JSON safe to delete."""
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
        # task-X_acq-Y_run-NN — strip _echo-N and _part-<x> (events are shared
        # across echoes and magnitude/phase) before generating the events stem.
        stem = bold.name.split("_bold")[0]
        stem = re.sub(r"_echo-[0-9]+", "", stem)
        stem = re.sub(r"_part-[A-Za-z0-9]+", "", stem)
        if stem in seen:
            continue
        seen.add(stem)
        events = func_dir / f"{stem}_events.tsv"
        if events.exists():
            continue
        events.write_text("onset\tduration\ttrial_type\n")


# Modality suffixes that dcm2niix `-f %H` inserts between the run entity
# and `_recording-LABEL` for physio files routed to derivatives/scanner/.
# Per the BIDS physiological-recordings spec the physio filename must inherit
# only the modality-shared entities (sub/ses/task/acq/ce/rec/dir/run) and the
# `_recording-LABEL` token — the parent modality suffix (`_bold`, `_sbref`, ...)
# does NOT belong in the physio filename. Strip it here.
_PHYSIO_INFIX_RE = re.compile(
    r"_(?:bold|sbref|dwi|asl|epi|m0scan|cbv|T1w|T2w|T2starw|FLAIR|PDw|angio)"
    r"(?=_recording-)"
)


def _rescue_physio_recordings(bids_root: Path, strict: bool) -> tuple[int, int]:
    """Move physio recordings out of `derivatives/scanner/<sub>/[<ses>/]func/`
    into the main BIDS tree, normalising the filename to BIDS spec.

    dcm2niix `-f %H` routes physio (`(7FE1,1010)` payload from Siemens Raw
    Data Storage SOPs) into the `derivatives/scanner/` subtree with a name
    like
        sub-X_task-Y_acq-Z_run-N_bold_recording-LABEL_physio.tsv.gz
    The `_bold` (or `_sbref`/`_dwi`/...) infix is non-spec — per
    https://bids-specification.readthedocs.io/en/stable/modality-specific-files/physiological-recordings.html
    the physio filename inherits sub/ses/task/acq/ce/rec/dir/run but NOT the
    modality suffix. This pass:
      1. Walks `derivatives/scanner/sub-X/.../func/` for `*_recording-*_physio.{tsv.gz,json}`.
      2. Strips the non-spec modality infix (`_PHYSIO_INFIX_RE`).
      3. Injects `_ses-Y` into the filename when the main tree pinned a
         session (single-session subjects only — multi-session detection
         needs acquisition-time matching that the source TSV doesn't expose).
      4. Moves the file into `<bids_root>/sub-X/[ses-Y/]func/`.

    Must run BEFORE `_drop_derivatives` so the source files still exist.
    Collision (destination already present from a curated re-run) leaves
    the source in place — non-destructive on hand-built trees.

    Known limitation: when dcm2niix produces multiple physio series whose
    ProtocolName/SeriesDescription share a stem after reproin parsing
    (e.g. a main BOLD's PhysioLog + the matching SBRef's PMU), the C side
    writes both to the same filename and the second overwrites the first.
    The rescue here recovers whatever survived in derivatives/scanner/.
    Splitting them needs a dcm2niix-side filename disambiguation; tracked
    separately.

    Returns (rescued, skipped). `skipped` counts physio files left behind
    on the source side under `derivatives/scanner/` — multi-session subjects
    (no acquisition-time matching against the provenance TSV yet), destination
    collisions (the curated file already exists), or per-file rename errors.
    Audit 2026-06-11 H1: the post-process pass MUST consult this count and
    refuse to drop derivatives when any physio was skipped, otherwise the
    Pass-4 sweep silently deletes recordings the rescue couldn't relocate.
    """
    deriv_root = bids_root / "derivatives" / "scanner"
    if not deriv_root.is_dir():
        return (0, 0)
    rescued = 0
    skipped = 0
    for sub_dir in sorted(deriv_root.iterdir()):
        if not sub_dir.is_dir() or not sub_dir.name.startswith("sub-"):
            continue
        sub_token = sub_dir.name
        # Determine destination session from the main tree (this runs AFTER
        # _propagate_session has moved files into sub-X/ses-Y/).
        main_sub_dir = bids_root / sub_token
        ses_token: Optional[str] = None
        multi_session = False
        if main_sub_dir.is_dir():
            ses_dirs = sorted(d for d in main_sub_dir.iterdir()
                              if d.is_dir() and d.name.startswith("ses-"))
            if len(ses_dirs) == 1:
                ses_token = ses_dirs[0].name.split("-", 1)[1]
            elif len(ses_dirs) > 1:
                # Multi-session subject — defer; the rescue would need
                # acquisition-time matching against the provenance TSV.
                multi_session = True
        # Walk derivatives for physio files. rglob covers both
        # derivatives/scanner/sub-X/func/ and derivatives/scanner/sub-X/ses-Y/func/.
        for func_dir in sub_dir.rglob("func"):
            if not func_dir.is_dir():
                continue
            # Group companions (.tsv.gz + .json) by recording stem so each
            # recording moves as one family — a collision on one companion must
            # not strand the other (audit 2026-06-20 cycle-3 F1).
            stems: set[str] = set()
            for p in func_dir.glob("*_recording-*_physio.tsv.gz"):
                stems.add(p.name[:-len(".tsv.gz")])
            for p in func_dir.glob("*_recording-*_physio.json"):
                stems.add(p.name[:-len(".json")])
            if not stems:
                continue
            if multi_session:
                # Tally every recording we leave behind so the caller knows the
                # `_drop_derivatives` sweep would destroy data.
                skipped += len(stems)
                print(f"reproinx: physio rescue skipped for {sub_token} "
                      f"({len(stems)} recording(s); multi-session subjects not yet supported)",
                      file=sys.stderr)
                if strict:
                    raise RuntimeError(
                        f"strict: physio rescue skipped for multi-session {sub_token}")
                continue
            for stem in sorted(stems):
                new_name = _PHYSIO_INFIX_RE.sub("", stem)
                if ses_token is not None:
                    new_name = _inject_session_into_name(new_name, sub_token, ses_token)
                    dest_dir = bids_root / sub_token / f"ses-{ses_token}" / "func"
                else:
                    dest_dir = bids_root / sub_token / "func"
                try:
                    # _move_stem_files is all-or-nothing: it preflights every
                    # companion and raises FileExistsError if ANY destination
                    # exists, so a curated .json can't strand the .tsv.gz.
                    if _move_stem_files(func_dir / stem, dest_dir / new_name) > 0:
                        rescued += 1
                except FileExistsError:
                    skipped += 1
                    print(f"reproinx: physio rescue collision — kept {func_dir / stem} "
                          f"(destination in {dest_dir} already present)",
                          file=sys.stderr)
                    if strict:
                        raise RuntimeError(
                            f"strict: physio rescue collision for {stem}")
                except OSError as e:
                    skipped += 1
                    print(f"reproinx: physio rescue failed for {func_dir / stem}: {e}",
                          file=sys.stderr)
                    if strict:
                        raise
    return (rescued, skipped)


def _drop_derivatives(out_root: Path, skip_roots: Optional[set] = None) -> int:
    """Remove `derivatives/scanner/` ONLY — the literal subdir dcm2niix `-f %H`
    routes scouts and DERIVED-flagged images (FA, ColFA, TENSOR_B0, scout
    localizers, physio) into. Curated subtrees like `derivatives/fmriprep/`,
    `derivatives/mriqc/`, `derivatives/freesurfer/` are left untouched.

    If `derivatives/` becomes empty after dropping `scanner/`, the parent is
    also removed (cosmetic). Returns the number of `scanner/` subtrees removed.

    `skip_roots` (audit 2026-06-11 H1): set of BIDS roots whose
    derivatives/scanner/ should be preserved (e.g. because
    `_rescue_physio_recordings` left un-rescued physio behind that would
    otherwise be deleted). Caller already warned the user; this just honors it.
    """
    skip = skip_roots or set()
    removed = 0
    for root in _bids_roots(_walk_subjects(out_root)):
        if root in skip:
            continue
        scanner = root / "derivatives" / "scanner"
        if scanner.is_dir():
            shutil.rmtree(scanner)
            removed += 1
            parent = root / "derivatives"
            try:
                # rmdir only if empty — never delete a parent containing
                # curated derivatives we don't own.
                parent.rmdir()
            except OSError:
                pass
    return removed


def _nifti_ndim(nii_path: Path) -> Optional[int]:
    """Return the NIfTI volume count (dim[4]) from the header, treating dim[0]<4
    or dim[4]<=1 as 3D. None on read error."""
    hdr = _read_nifti_header(nii_path)
    if hdr is None:
        return None
    if struct.unpack("<i", hdr[:4])[0] == 348:
        bo = "<"
    elif struct.unpack(">i", hdr[:4])[0] == 348:
        bo = ">"
    else:
        return None
    dim = struct.unpack(bo + "8h", hdr[40:56])
    if int(dim[0]) < 4:
        return 1
    return int(dim[4]) if dim[4] > 0 else 1


def _bidsguess_remove_discard(session_dir: Path) -> int:
    """Drop the entire `discard/` subdir under a session (localizers + scouts).
    BIDS has no `discard` datatype; bids-validator emits NOT_INCLUDED for every
    file inside. Returns 1 if a directory was removed, else 0."""
    d = session_dir / "discard"
    if not d.is_dir():
        return 0
    shutil.rmtree(d)
    return 1


def _bidsguess_singlevol_dwi_patterns(session_dir: Path,
                                       bids_root: Path) -> list[str]:
    """Find single-volume `*_dwi.nii*` artifacts under a session and return
    their full sidecar set as POSIX paths relative to `bids_root`. Single-vol
    DWI is BIDS-invalid two ways at once (DWI requires bvec/bval, but having
    them with <2 volumes triggers VOLUME_COUNT_MISMATCH); listing the whole
    artifact in .bidsignore is the cleanest pragmatic resolution. Returns
    [] when the dwi/ dir is absent or no single-volume DWI exists."""
    dwi_dir = session_dir / "dwi"
    if not dwi_dir.is_dir():
        return []
    patterns: list[str] = []
    for nii in sorted(list(dwi_dir.glob("*_dwi.nii")) +
                      list(dwi_dir.glob("*_dwi.nii.gz"))):
        n = _nifti_ndim(nii)
        if n is None or n >= 2:
            continue
        stem = nii.name[:-len(".nii.gz")] if nii.name.endswith(".nii.gz") else nii.stem
        for ext in (".nii", ".nii.gz", ".json", ".bvec", ".bval"):
            companion = dwi_dir / f"{stem}{ext}"
            if companion.is_file():
                try:
                    patterns.append(companion.relative_to(bids_root).as_posix())
                except ValueError:
                    pass
    return patterns


def _bidsguess_demote_3d_bold(session_dir: Path) -> int:
    """Rename 3D `*_bold` files to `*_sbref` (single-band reference). BIDS
    requires `_bold` scans to be 4D; dcm2niix's legacy %h heuristics can route
    single-volume EPI into func/. `_sbref` is the closest BIDS-compliant suffix
    for an EPI scan paired with a multi-volume bold acquisition. Renames the
    `.nii`/`.nii.gz` and `.json` together (bold files do not carry
    `.bvec`/`.bval` so those extensions don't appear at the source). Any
    `_events.tsv` is dropped — sbref has no events sidecar. Returns the
    number of stems renamed."""
    func_dir = session_dir / "func"
    if not func_dir.is_dir():
        return 0
    renamed = 0
    for nii in sorted(list(func_dir.glob("*_bold.nii")) +
                      list(func_dir.glob("*_bold.nii.gz"))):
        n = _nifti_ndim(nii)
        if n is None or n >= 2:
            continue
        if nii.name.endswith(".nii.gz"):
            stem = nii.name[:-len(".nii.gz")]
            nii_ext = ".nii.gz"
        else:
            stem = nii.stem
            nii_ext = ".nii"
        if not stem.endswith("_bold"):
            continue
        new_stem = stem[:-len("_bold")] + "_sbref"
        # Preflight: refuse to clobber a pre-existing _sbref family. POSIX
        # rename silently replaces the target, so without this check a
        # demote could overwrite a real single-band reference. Audit M4:
        # check BOTH .nii and .nii.gz so a `_sbref.nii` blocks demoting
        # `_bold.nii.gz` even when their extensions don't match.
        collision = False
        for ext in (".nii", ".nii.gz", ".json"):
            if (func_dir / f"{new_stem}{ext}").exists():
                collision = True
                break
        if collision:
            print(f"reproinx: skipping 3D bold demote for {stem} — "
                  f"{new_stem} target already exists",
                  file=sys.stderr)
            continue
        # _events.tsv is a bold-only sidecar and would now be orphaned;
        # _sbref has no events. Drop it rather than carry it forward.
        events = func_dir / f"{stem}_events.tsv"
        if events.is_file():
            events.unlink()
        for ext in (nii_ext, ".json"):
            src = func_dir / f"{stem}{ext}"
            dst = func_dir / f"{new_stem}{ext}"
            if src.is_file():
                src.rename(dst)
        renamed += 1
    return renamed


def _part_from_imagetype(data: dict) -> Optional[str]:
    """BIDS `part` label from DICOM ImageType (0008,0008). None if absent or
    ambiguous (more than one component token, e.g. Philips MAGNITUDE+PHASE)."""
    it = data.get("ImageType")
    if not isinstance(it, list):
        return None
    toks = {str(x).upper() for x in it}
    found = [part for key, part in (("PHASE", "phase"), ("MAGNITUDE", "mag"),
                                    ("REAL", "real"), ("IMAGINARY", "imag"))
             if key in toks]
    return found[0] if len(found) == 1 else None


def _part_of(data: dict) -> Optional[str]:
    """BIDS `part`. Prefer dcm2niix's explicit `_part-<x>` in BidsGuess (the C
    side sets it from CSA isHasPhase even when public ImageType is absent), else
    derive from ImageType. None if ambiguous (multiple/conflicting component
    indications, e.g. a Philips `_part-phase_part-mag` BidsGuess): a genuine
    single part cannot be invented, so leave the file untouched."""
    g = data.get("BidsGuess")
    if isinstance(g, list) and len(g) == 2 and isinstance(g[1], str):
        ms = re.findall(r"_part-(mag|phase|real|imag)(?=_|$)", g[1])  # lookahead: don't consume the shared '_'
        if len(ms) == 1:
            return ms[0]
        if len(ms) > 1:
            return None
    return _part_from_imagetype(data)


def _func_suffix(data: dict, token: str) -> Optional[str]:
    """`bold`/`sbref`, preferring BidsGuess (a phase SBRef can collide into a
    `_bold`-named file), else the filename token minus its collision letter."""
    g = data.get("BidsGuess")
    if isinstance(g, list) and len(g) == 2 and isinstance(g[1], str):
        last = g[1].rsplit("_", 1)[-1]
        if last in ("bold", "sbref"):
            return last
    if token.startswith("sbref"):
        return "sbref"
    if token.startswith("bold"):
        return "bold"
    return None


def _resolve_part_entities(session_dir: Path) -> int:
    """Insert BIDS `part-mag`/`part-phase` and resolve dcm2niix magnitude/phase
    collision suffixes for func series. A multi-echo BOLD with phase produces
    magnitude+phase series that collide onto one %H stem; dcm2niix appends
    `a`/`b` letters (and can mislabel a phase SBRef as `_bold`), which are then
    unusable BIDS. `part` comes from BidsGuess's explicit `_part-` (fallback
    ImageType), the true `bold`/`sbref` suffix from BidsGuess; the family is
    renamed to `..._part-<mag|phase>_<bold|sbref>`. Only fires for a (prefix,
    suffix) group containing a non-magnitude member. Group renames are
    all-or-nothing: a mid-group failure rolls back and re-raises rather than
    leave mixed collision/part names. Returns stems renamed."""
    func_dir = session_dir / "func"
    if not func_dir.is_dir():
        return 0
    groups: dict[tuple, list] = {}
    unresolved_prefixes: set[str] = set()
    for jp in sorted(func_dir.glob("*.json")):
        stem = jp.stem
        entities, token = (stem.rsplit("_", 1) + [""])[:2]
        if not (token.startswith("bold") or token.startswith("sbref")):
            continue
        prefix = re.sub(r"_part-[A-Za-z0-9]+$", "", entities)
        data = _load_json(jp)
        part = _part_of(data) if isinstance(data, dict) else None
        suffix = _func_suffix(data, token) if isinstance(data, dict) else None
        if part is None or suffix is None:
            unresolved_prefixes.add(prefix)  # unclassifiable sibling taints its whole family
            continue
        groups.setdefault((prefix, suffix), []).append((stem, part))
    renamed = 0
    resolved_phase: list[Path] = []
    for (prefix, suffix), members in groups.items():
        if prefix in unresolved_prefixes:
            print(f"reproinx: skipping part-entity resolution for {prefix} — unclassifiable sibling in family", file=sys.stderr)
            continue
        if {p for _s, p in members} <= {"mag"}:
            continue  # no phase/real/imag sibling: part entity not needed
        # Preflight the whole group against the full BIDS family before any move.
        plan: list[tuple[str, str, str]] = []
        dsts: set[str] = set()
        conflict = False
        for stem, part in members:
            dst = f"{prefix}_part-{part}_{suffix}"
            if dst == stem:
                continue
            if dst in dsts or any((func_dir / f"{dst}{ext}").exists()
                                  for ext in _BIDS_EXTS):
                conflict = True
                break
            dsts.add(dst)
            plan.append((stem, dst, part))
        if conflict:
            print(f"reproinx: skipping part-entity resolution for "
                  f"{prefix}_{suffix} — target collision", file=sys.stderr)
            continue
        done: list[tuple[str, str, str]] = []
        try:
            for stem, dst, part in plan:
                if _move_stem_files(func_dir / stem, func_dir / dst) > 0:
                    done.append((stem, dst, part))
        except Exception:
            for stem, dst, _p in reversed(done):  # all-or-nothing per group
                try:
                    _move_stem_files(func_dir / dst, func_dir / stem)
                except Exception:
                    pass
            raise
        renamed += len(done)
        # Collect the phase sidecar of EVERY fired group (not only moved
        # members) so an already-canonical phase file — or a rerun after a
        # move-then-Units-write failure — still gets its required Units.
        if any(part == "phase" for _s, part in members):
            resolved_phase.append(func_dir / f"{prefix}_part-phase_{suffix}.json")
    # BIDS requires Units for phase data. Restrict to the phase files just
    # resolved and to Siemens (scanner-scaled phase -> "arbitrary"; other
    # vendors / rad scaling are out of scope). Never overwrite existing Units.
    for jp in resolved_phase:
        if not jp.is_file():
            continue
        d = _load_json(jp)
        if (isinstance(d, dict) and "Units" not in d and
                str(d.get("Manufacturer", "")).lower().startswith("siemens")):
            d["Units"] = "arbitrary"
            _save_json(jp, d)
    return renamed


_BIDSGUESS_COLLISION_RE = re.compile(
    # BIDS suffix tokens that the C-side setBidsHeuristics / vendor BidsGuess
    # paths can emit. Must include the DWI scanner-derivative suffixes added
    # in commit 2dad442 (FA / ADC / colFA / expADC / trace / S0map / TENSOR);
    # without them a duplicate FA series collapses to e.g. `_FAa.nii.gz` and
    # the .bidsignore sweep would miss it.
    r"_(magnitude\d|phasediff|phase\d|fieldmap|bold|sbref|T1w|T2w|FLAIR|"
    r"PDw|T2starw|UNIT1|inplaneT[12]|MEGRE|MESE|VFA|IRT1|MP2RAGE|MPM|MTS|MTR|"
    r"dwi|epi|m0scan|asl|aslcontext|cbv|defacemask|"
    r"FA|ADC|colFA|expADC|trace|S0map|TENSOR|svs|mrsi|unloc|mrsref)[a-z]"
    r"(\.json|\.nii(\.gz)?|\.bvec|\.bval|\.tsv)$"
)


DEFAULT_MIN_VOLUMES = 5  # timeseries shorter than this are treated as candidates
                         # for distortion maps rather than func/dwi data.


def _bids_suffix(stem: str) -> str:
    """BIDS suffix = the final underscore-delimited token of a file stem."""
    return stem.split("_")[-1]


def _pe_dir_label(pe) -> Optional[str]:
    """Sidecar `PhaseEncodingDirection` (i/j/k with optional +/-) -> an
    arbitrary-but-distinct BIDS `dir-<label>` value, sanitised of the `-` that
    BIDS entity labels forbid: `j`/`j+` -> `J`, `j-` -> `Jn`.

    The label only needs to *differ* between the two polarities of a PEPOLAR
    pair (BIDS "Case 4: Multiple phase-encoded directions (pepolar)"); the
    axis-letter + `n`-for-negative is a deterministic, slice-orientation-
    agnostic choice (AP/LR/IS would require knowing axial vs sagittal vs
    coronal). Returns None when no usable polarity is present."""
    if not pe:
        return None
    s = str(pe).strip()
    if not s or s[0].lower() not in ("i", "j", "k"):
        return None
    return s[0].upper() + ("n" if s.endswith("-") else "")


_FMAP_EPI_ENTITY_ORDER = ("sub", "ses", "acq", "ce", "dir", "run")


def _to_fmap_epi_stem(src_stem: str, dir_label: str) -> str:
    """Rebuild a func/dwi stem as an fmap `_epi` stem: drop `task-`, set/replace
    `dir-<dir_label>`, keep sub/ses/acq/run in canonical BIDS fmap entity order,
    and swap the suffix for `epi`. Unknown entities (rare) are preserved after
    the known ones, before the suffix."""
    ents: dict[str, str] = {}
    extra: list[str] = []
    for tok in src_stem.split("_"):
        if "-" not in tok:
            continue  # the trailing suffix token (e.g. "bold") has no '-'
        k, v = tok.split("-", 1)
        if k == "task":
            continue
        if k in _FMAP_EPI_ENTITY_ORDER:
            ents[k] = v
        else:
            extra.append(tok)
    # Prefer an anatomical `dir-` already present on the source stem (dcm2niix
    # derives `dir-AP`/`dir-PA` from PhaseEncodingDirection + image orientation,
    # and a reproin protocol may state `dir-ap`/`dir-pa` explicitly). Only fall
    # back to the orientation-agnostic PE axis label (`J`/`Jn`) when the stem
    # carries no usable polarity of its own — the two members of a PEPOLAR pair
    # then still differ by their opposite PE signs.
    if "dir" not in ents:
        ents["dir"] = dir_label
    parts = [f"{k}-{ents[k]}" for k in _FMAP_EPI_ENTITY_ORDER if k in ents]
    parts.extend(extra)
    return "_".join(parts) + "_epi"


def _strip_orphan_runs(session_dir: Path) -> int:
    """Remove a `_run-NN` entity that disambiguates nothing.

    The Unknown-rescue assigns a collision run to two series that share a
    BidsGuess stem; `_reclassify_session` may then move one of them to another
    datatype (e.g. a short reverse-PE dwi -> fmap/_epi), leaving the survivor
    the sole owner of its stem yet still carrying the now-meaningless run.
    Strip the run when (a) the file is the only member of its run-stripped stem
    within its datatype dir, AND (b) the series' own ProtocolName carries no
    explicit numeric run (so operator-specified runs survive even on a lone
    series). Physio recordings (no `.nii`) are untouched. Returns the number of
    series renamed."""
    if not session_dir.is_dir():
        return 0
    count = 0
    for dt_dir in session_dir.iterdir():
        if not dt_dir.is_dir() or dt_dir.name.startswith("ses-"):
            continue
        groups: dict[str, list[str]] = {}
        for nii in list(dt_dir.glob("*.nii.gz")) + list(dt_dir.glob("*.nii")):
            stem = (nii.name[:-len(".nii.gz")]
                    if nii.name.endswith(".nii.gz") else nii.stem)
            groups.setdefault(_RUN_ENTITY_RE.sub("", stem), []).append(stem)
        for base, members in groups.items():
            if len(members) != 1:
                continue  # run is disambiguating — keep
            stem = members[0]
            if not _RUN_ENTITY_RE.search(stem):
                continue  # nothing to strip
            jp = dt_dir / f"{stem}.json"
            prot_run = False
            try:
                data = _load_json(jp)
                for fld in ("ProtocolName", "SeriesDescription"):
                    if _RUN_PROTOCOL_RE.search(str(data.get(fld, ""))):
                        prot_run = True
                        break
            except (OSError, ValueError, json.JSONDecodeError):
                pass
            if prot_run:
                continue  # operator-specified run — preserve
            try:
                _move_stem_files(dt_dir / stem, dt_dir / base)
                count += 1
            except FileExistsError:
                pass
    return count


def _reclassify_session(session_dir: Path, min_volumes: int) -> int:
    """Cross-series BIDS reclassification within one session.

    dcm2niix classifies each series in isolation; here we use the full set of
    series in a session to resolve ambiguous intent (mirrors reproin-namer):

      R1  A short EPI (< `min_volumes` volumes) currently in func/ (`_bold`) or
          dwi/ (`_dwi`), NOT already `_sbref` (CMRR burns SBRef into the series
          description so dcm2niix already names true single-band references),
          becomes a PEPOLAR/topup distortion map `fmap/..._dir-<L>_epi` WHEN a
          long-EPI sibling (>= `min_volumes`-volume `_bold` or `_dwi`) exists in
          the session to undistort. `<L>` = sanitised PhaseEncodingDirection.
          (An SE pepolar pair legitimately undistorts a GE fMRI, so we do not
          gate on sequence type — only volume count + a long EPI sibling.)

      R2  An `anat/..._T2w` whose in-plane (x,y) matrix matches a session func
          `_bold` is a co-planar reference -> `_inplaneT2`.

    Whole sidecar families (.nii(.gz)/.json/.bval/.bvec) move together; a dwi
    b0 promoted to `_epi` sheds its `.bval`/`.bvec`, and an orphaned func
    `_events.tsv` is dropped when a `_bold` becomes `_epi`. Runs BEFORE the
    `_sbref` demote / single-vol-dwi `.bidsignore` fallbacks so those only see
    genuine leftovers; the new `_epi` fmaps are picked up by `_populate_b0_fields`.
    Returns the number of series reclassified."""
    if not session_dir.is_dir():
        return 0
    # Snapshot per-series features BEFORE any move (decisions use the original
    # layout). Each entry: (nii_path, datatype, stem, suffix, ndim, shape3, pe).
    series: list[tuple] = []
    for dt in ("func", "dwi", "anat"):
        d = session_dir / dt
        if not d.is_dir():
            continue
        for nii in sorted(list(d.glob("*.nii")) + list(d.glob("*.nii.gz"))):
            stem = nii.name[:-7] if nii.name.endswith(".nii.gz") else nii.stem
            jp = d / f"{stem}.json"
            vol = _imaging_volume(jp) if jp.is_file() else None
            pe = None
            if jp.is_file():
                try:
                    pe = _load_json(jp).get("PhaseEncodingDirection")
                except (OSError, ValueError, json.JSONDecodeError):
                    pe = None
            series.append((nii, dt, stem, _bids_suffix(stem),
                           _nifti_ndim(nii), vol[1] if vol else None, pe))

    long_epi = any(dt in ("func", "dwi") and suf in ("bold", "dwi")
                   and (nd or 0) >= min_volumes
                   for (_n, dt, _s, suf, nd, _sh, _pe) in series)
    func_xy = {tuple(sh[:2]) for (_n, dt, _s, suf, _nd, sh, _pe) in series
               if dt == "func" and suf == "bold" and sh}

    count = 0
    for (nii, dt, stem, suf, ndim, shape3, pe) in series:
        # R1: short EPI with a long-EPI sibling -> fmap/_dir-<L>_epi
        # Skip multi-echo series: BIDS pepolar `_epi` does not permit the `echo`
        # entity (ENTITY_NOT_IN_RULE), stripping it would collide echo-1/echo-2,
        # and a multi-echo acquisition is structurally a real imaging series, not
        # a quick single-echo distortion scan. Leave it as `_bold`/`_dwi`.
        if (dt in ("func", "dwi") and suf in ("bold", "dwi") and long_epi
                and ndim is not None and ndim < min_volumes
                and not _ECHO_TOKEN_RE.search(stem)):
            label = _pe_dir_label(pe)
            if label is None:
                continue  # need a polarity to name the required dir- entity
            new_stem = _to_fmap_epi_stem(stem, label)
            dst = session_dir / "fmap" / new_stem
            try:
                _move_stem_files(nii.parent / stem, dst)
            except FileExistsError:
                continue
            # An `_epi` fmap carries no diffusion table or events sidecar.
            for ext in (".bval", ".bvec"):
                stray = dst.parent / f"{new_stem}{ext}"
                if stray.is_file():
                    stray.unlink()
            ev = nii.parent / f"{stem[:-(len(suf) + 1)]}_events.tsv"
            if ev.is_file():
                ev.unlink()
            count += 1
            continue
        # R2: co-planar T2 (matches a func in-plane) -> _inplaneT2
        if (dt == "anat" and suf == "T2w" and shape3
                and tuple(shape3[:2]) in func_xy):
            new_stem = f"{stem[:-len('_T2w')]}_inplaneT2"
            try:
                _move_stem_files(nii.parent / stem, nii.parent / new_stem)
                count += 1
            except FileExistsError:
                pass
    return count


def _bidsguess_collision_files(bids_root: Path) -> list[str]:
    """Locate files where dcm2niix appended an `a`/`b`/`c` collision suffix to
    a known BIDS modality token (e.g. `_magnitude1a.json`, `_phasediffb.nii.gz`).
    The %H path renames these to `__dup-NN` using provenance; the %h path has
    no provenance, so we list them for .bidsignore instead. Returns paths
    relative to `bids_root`, POSIX form."""
    hits = []
    for path in bids_root.rglob("sub-*"):
        if not path.is_file():
            continue
        if _BIDSGUESS_COLLISION_RE.search(path.name):
            try:
                rel = path.relative_to(bids_root).as_posix()
            except ValueError:
                continue
            hits.append(rel)
    return sorted(hits)


def _bidsguess_unknown_leftover_files(bids_root: Path) -> list[str]:
    """List files still living under `<bids_root>/Unknown/` after the rescue
    pass (e.g. series whose `BidsGuess` was `discard`/`derived`, or whose
    provenance row lacked PatientID under `-ba y`). Adding them to
    .bidsignore prevents NOT_INCLUDED while preserving the files on disk.
    Returns POSIX paths relative to `bids_root`."""
    unknown = bids_root / "Unknown"
    if not unknown.is_dir():
        return []
    hits: list[str] = []
    for path in sorted(unknown.rglob("*")):
        if not path.is_file():
            continue
        try:
            hits.append(path.relative_to(bids_root).as_posix())
        except ValueError:
            pass
    return hits


def _bidsguess_write_bidsignore(bids_root: Path, patterns: list[str]) -> int:
    """Append (or create) `.bidsignore` listing the collision-suffix files.
    Preserves any user-curated patterns already in the file (line-equality
    dedup). Returns the number of new lines added."""
    if not patterns:
        return 0
    ignore = bids_root / ".bidsignore"
    existing: list[str] = []
    if ignore.is_file():
        try:
            existing = ignore.read_text(encoding="utf-8").splitlines()
        except OSError:
            existing = []
    seen = set(s.strip() for s in existing if s.strip())
    added = []
    for p in patterns:
        if p not in seen:
            added.append(p)
            seen.add(p)
    if not added:
        return 0
    lines = existing[:]
    if lines and lines[-1].strip() != "":
        lines.append("")
    lines.append("# reproinx: files dcm2niix could not place cleanly in BIDS")
    lines.extend(added)
    ignore.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return len(added)


def _bidsguess_cleanup(out_root: Path, strict: bool,
                       min_volumes: int = DEFAULT_MIN_VOLUMES) -> None:
    """Bidsguess-specific second pass over a %h-converted tree. Runs BEFORE
    the generic reproinx passes so scans.tsv / participants.tsv see the
    cleaned layout."""
    # Aggregate .bidsignore patterns per bids-root across all sessions, then
    # write once. The session loop computes its bids_root as ses.parent.parent
    # (sub-X/ses-Y/), which mirrors how reproinx _bids_roots discovers roots.
    by_root: dict[Path, list[str]] = {}
    for ses in _walk_sessions(out_root):
        bids_root = _bids_root_for_session(ses)
        # Cross-series reclassification FIRST: short EPI -> fmap/_epi, co-planar
        # T2 -> _inplaneT2. Runs before the demote/single-vol-dwi fallbacks so
        # those only handle genuine leftovers; new fmaps feed _populate_b0_fields.
        try:
            n = _reclassify_session(ses, min_volumes)
            if n > 0:
                print(f"  {ses}: reclassified {n} series via session context",
                      file=sys.stderr)
        except Exception as e:
            print(f"reproinx: session reclassification failed for {ses}: {e}",
                  file=sys.stderr)
            if strict:
                raise
        # Drop collision runs left non-disambiguating by the reclassification
        # above (e.g. the lone dwi after its reverse-PE sibling became fmap/_epi).
        try:
            n = _strip_orphan_runs(ses)
            if n > 0:
                print(f"  {ses}: stripped {n} non-disambiguating run(s)",
                      file=sys.stderr)
        except Exception as e:
            print(f"reproinx: orphan-run strip failed for {ses}: {e}",
                  file=sys.stderr)
            if strict:
                raise
        try:
            n = _bidsguess_remove_discard(ses)
            if n > 0:
                print(f"  {ses}: removed discard/", file=sys.stderr)
        except Exception as e:
            print(f"reproinx: discard removal failed for {ses}: {e}",
                  file=sys.stderr)
            if strict:
                raise
        try:
            n = _bidsguess_demote_3d_bold(ses)
            if n > 0:
                print(f"  {ses}: demoted {n} 3D _bold → _sbref",
                      file=sys.stderr)
        except Exception as e:
            print(f"reproinx: 3D bold demote failed for {ses}: {e}",
                  file=sys.stderr)
            if strict:
                raise
        try:
            n = _resolve_part_entities(ses)
            if n > 0:
                print(f"  {ses}: resolved {n} func part-mag/part-phase file(s)",
                      file=sys.stderr)
        except Exception as e:
            print(f"reproinx: part-entity resolution failed for {ses}: {e}",
                  file=sys.stderr)
            if strict:
                raise
        try:
            pats = _bidsguess_singlevol_dwi_patterns(ses, bids_root)
            if pats:
                by_root.setdefault(bids_root, []).extend(pats)
                print(f"  {ses}: marked {len(pats)} single-volume dwi "
                      "artifact file(s) for .bidsignore", file=sys.stderr)
        except Exception as e:
            print(f"reproinx: single-vol dwi scan failed for {ses}: {e}",
                  file=sys.stderr)
            if strict:
                raise
    # Include any subject-less bids_roots that only have Unknown/ leftovers,
    # so their .bidsignore picks up the residual files.
    candidate_roots = set(_bids_roots(_walk_subjects(out_root)))
    for unknown in out_root.rglob("Unknown"):
        if unknown.is_dir() and not any(part == "derivatives" for part in unknown.parts):
            candidate_roots.add(unknown.parent)
    for root in sorted(candidate_roots):
        try:
            collision = _bidsguess_collision_files(root)
            singlevol = by_root.get(root, [])
            leftover = _bidsguess_unknown_leftover_files(root)
            patterns = sorted(set(collision) | set(singlevol) | set(leftover))
            n = _bidsguess_write_bidsignore(root, patterns)
            if n > 0:
                print(f"  {root}: added {n} entries to .bidsignore",
                      file=sys.stderr)
        except Exception as e:
            print(f"reproinx: .bidsignore write failed for {root}: {e}",
                  file=sys.stderr)
            if strict:
                raise


_DATE_SHIFT_REF = _dt.date(1925, 1, 1)  # BIDS-recommended de-identification anchor
_TS_SESSION_RE = re.compile(r"ses-\d{8}T\d{6}")  # reproin timestamp-derived session


def _parse_acq_dt(s) -> Optional[_dt.datetime]:
    """Parse an ISO acq datetime ('YYYY-MM-DDThh:mm:ss[.ffffff]'). None on fail."""
    if not s or str(s) == "n/a":
        return None
    try:
        return _dt.datetime.fromisoformat(str(s))
    except (ValueError, TypeError):
        return None


def _shift_scans_tsv_dates(scans: Path, offset: _dt.timedelta) -> None:
    """Subtract `offset` from the `acq_time` column of one `_scans.tsv`,
    preserving the header, CRLF line endings, and any non-datetime cells.
    The column is resolved from the header (not assumed to be column 2), so a
    curated/reordered TSV shifts the right column; a TSV with no `acq_time`
    column has no dates and is left untouched. Fail-closed: a read OR write
    error raises (so --shift-dates cannot exit 0 leaving real dates)."""
    lines = scans.read_text(encoding="utf-8").splitlines()  # OSError -> fatal
    if len(lines) < 2:
        return
    header = lines[0].split("\t")
    try:
        acq_col = header.index("acq_time")
    except ValueError:
        return  # no acq_time column -> no dates in this TSV, nothing to shift
    out = [lines[0]]
    for line in lines[1:]:
        if not line.strip():
            out.append(line)
            continue
        cols = line.split("\t")
        if len(cols) > acq_col:
            dt = _parse_acq_dt(cols[acq_col])
            if dt is not None:
                cols[acq_col] = (dt - offset).isoformat()
        out.append("\t".join(cols))
    # Atomic write via a UNIQUE sibling temp + os.replace. NamedTemporaryFile
    # gives a collision-free name and a real file object (closed by `with`, so
    # no raw fd leaks on any error); newline="" writes the explicit CRLF verbatim.
    tmp = None
    try:
        with tempfile.NamedTemporaryFile(
                "w", encoding="utf-8", newline="", delete=False,
                dir=str(scans.parent), prefix=scans.name + ".", suffix=".tmp") as f:
            tmp = Path(f.name)
            f.write("\r\n".join(out) + "\r\n")
        os.replace(str(tmp), str(scans))
    except Exception:
        if tmp is not None:
            tmp.unlink(missing_ok=True)
        raise


def _scans_acq_dates(scans: Path) -> list:
    """Every parseable `acq_time` datetime in one `_scans.tsv` (header-resolved
    column). Fail-closed: a read error raises. `[]` when there is no acq_time
    column. Lets date de-identification discover dates that live ONLY in
    scans.tsv (a collated tree whose sidecars lack AcquisitionDateTime)."""
    lines = scans.read_text(encoding="utf-8").splitlines()  # OSError -> fatal
    if len(lines) < 2:
        return []
    header = lines[0].split("\t")
    try:
        acq_col = header.index("acq_time")
    except ValueError:
        return []
    out = []
    for line in lines[1:]:
        if not line.strip():
            continue
        cols = line.split("\t")
        if len(cols) > acq_col:
            dt = _parse_acq_dt(cols[acq_col])
            if dt is not None:
                out.append(dt)
    return out


def _sidecar_date(data: dict) -> Optional[_dt.datetime]:
    """Earliest-comparable datetime from ANY sidecar date field, for offset
    discovery: full `AcquisitionDateTime` / `AcquisitionDate`+`AcquisitionTime`
    (via `_acq_iso`), else a bare date-only `AcquisitionDate` anchored to
    midnight. Mirrors what `_shift_sidecar_dates` can rewrite, so discovery never
    misses a field the rewrite would shift. None if the dict carries no date."""
    dt = _parse_acq_dt(_acq_iso(data))
    if dt is not None:
        return dt
    ad = data.get("AcquisitionDate")
    if isinstance(ad, str) and len(ad) == 8 and ad.isdigit():
        try:
            return _dt.datetime(int(ad[:4]), int(ad[4:6]), int(ad[6:8]))
        except ValueError:
            return None
    return None


def _shift_sidecar_dates(data: dict, offset: _dt.timedelta) -> bool:
    """Shift every date-bearing field of a sidecar dict in place; True if changed.
    Covers `AcquisitionDateTime` and the date-only `AcquisitionDate` ('YYYYMMDD',
    the field `_acq_iso`/`_write_scans_tsv` can synthesize acq_time from); the
    time-only `AcquisitionTime` carries no date and is left untouched."""
    changed = False
    dt = _parse_acq_dt(data.get("AcquisitionDateTime"))
    if dt is not None:
        data["AcquisitionDateTime"] = (dt - offset).isoformat()
        changed = True
    ad = data.get("AcquisitionDate")
    if isinstance(ad, str) and len(ad) == 8 and ad.isdigit():
        try:
            data["AcquisitionDate"] = (
                _dt.date(int(ad[:4]), int(ad[4:6]), int(ad[6:8])) - offset
            ).strftime("%Y%m%d")
            changed = True
        except ValueError:
            pass
    return changed


def _shift_dates(out_root: Path) -> int:
    """De-identify acquisition dates (BIDS RECOMMENDED; opt-in via --shift-dates).

    For each subject, shift every acquisition datetime by a whole-day offset so
    the subject's EARLIEST scan lands on 1925-01-01, preserving time-of-day and
    all intra-subject intervals (sessions, runs). The earliest is discovered
    across ALL date sources — sidecar `AcquisitionDateTime`, sidecar
    `AcquisitionDate`+`AcquisitionTime`, and the `_scans.tsv` `acq_time` column —
    and all of them are rewritten (sidecar `AcquisitionDateTime` + `AcquisitionDate`,
    and scans.tsv `acq_time`); the time-only `AcquisitionTime` carries no date and
    is left untouched. The offset is per-subject, so cross-subject absolute timing
    is destroyed (privacy) while within-subject longitudinal structure is
    preserved. Returns subjects shifted.

    Completeness: also shifts any RETAINED `derivatives/` sidecars (which
    `_walk_subjects` skips) — present under `--keep-derivatives` or a
    physio-preserved root — and removes `.reproin_provenance.tsv` (+ `.bak`),
    which carry raw StudyDate/StudyTime/PatientID. Fail-closed: an unreadable
    date-bearing JSON/scans.tsv raises rather than being silently skipped.

    Limitation: timestamp-derived session labels (`ses-<YYYYMMDD...>`) are NOT
    renamed — that would mean moving directories and rewriting scans filenames.
    Use named sessions (`ses-pre`) to avoid leaking the date via the path."""
    def _strict_load(jp: Path) -> dict:
        # Fail-closed: an unreadable/malformed sidecar during de-identification
        # must abort, not be silently skipped — we cannot otherwise verify it
        # carries no date. A readable non-object JSON simply has no date field.
        try:
            data = _load_json(jp)
        except (OSError, ValueError, json.JSONDecodeError) as e:
            raise RuntimeError(f"cannot read {jp} for date-shift: {e}") from e
        return data if isinstance(data, dict) else {}

    shifted = 0
    offsets: dict[str, _dt.timedelta] = {}  # subject dir name -> whole-day shift
    subjects = _walk_subjects(out_root)  # non-derivatives top-level sub-* DIRS
    for sub in subjects:
        jsons = list(sub.rglob("*.json"))
        scans_files = list(sub.rglob("*_scans.tsv"))
        # Earliest acquisition across ALL date sources: sidecar AcquisitionDateTime
        # OR AcquisitionDate+AcquisitionTime (via _acq_iso), AND scans.tsv acq_time
        # (which _write_scans_tsv can synthesize from the date fields). Without the
        # scans.tsv source, a collated tree whose sidecars lack a date but whose
        # scans.tsv carries one would be skipped, leaving real dates (fail-open).
        earliest = None
        for jp in jsons:
            dt = _sidecar_date(_strict_load(jp))
            if dt is not None and (earliest is None or dt < earliest):
                earliest = dt
        for sc in scans_files:
            for dt in _scans_acq_dates(sc):
                if earliest is None or dt < earliest:
                    earliest = dt
        if earliest is None:
            continue
        offset = _dt.timedelta(days=(earliest.date() - _DATE_SHIFT_REF).days)
        offsets[sub.name] = offset  # record even if 0 so derivatives reuse it
        if offset.days == 0:
            continue
        for jp in jsons:
            data = _strict_load(jp)
            if _shift_sidecar_dates(data, offset):
                _save_json(jp, data)
        for scans in scans_files:
            _shift_scans_tsv_dates(scans, offset)
        shifted += 1
    # Timestamp-session leak warning, INDEPENDENT of whether any date was shifted
    # (a re-run / already-1925 tree has offset 0 but the ses-<YYYYMMDD...> dir
    # still encodes the real date in its path; renaming the dir is deferred —
    # moves files + rewrites scans filenames). Surface it so the user isn't
    # silently misled that --shift-dates fully de-identified the tree.
    for sub in subjects:
        leaky = sorted({p.name for p in sub.glob("ses-*")
                        if _TS_SESSION_RE.fullmatch(p.name)})
        if leaky:
            print(f"reproinx: WARNING {sub.name}: --shift-dates anchored "
                  f"sidecars/scans.tsv to {_DATE_SHIFT_REF.year}, but "
                  f"timestamp session dir(s) {leaky} still leak the real date "
                  f"via the path. Use named sessions to fully de-identify.",
                  file=sys.stderr)
    # Everything the per-subject pass did NOT already shift but which can still
    # carry a real date: retained derivatives/ survivors (--keep-derivatives or a
    # physio-preserved root, skipped by _walk_subjects) AND subjectless root-level
    # output — chiefly Unknown/*.json (CT intentionally left in Unknown/, or a
    # wholly-unrescued series) plus any stray *_scans.tsv. The per-subject pass
    # covered non-derivatives sub-*/** only, so sweep the rest here (both *.json
    # AND *_scans.tsv): reuse the owning subject's offset when the path has a
    # sub-X component, else anchor the file's own earliest date to the reference
    # year (per-file is acceptable — this output is not longitudinally grouped).
    # Guarantees no real acquisition date survives anywhere in the tree.
    def _already_shifted(p: Path) -> bool:
        # True iff the per-subject pass already handled this file: it is strictly
        # under one of the walked subject DIRS. Must NOT test path *components*
        # (p.parts) for a "sub-" prefix — the filename is a component, so a
        # subjectless survivor literally named e.g. Unknown/sub-x_ct.json (subject
        # known but classification failed → lands in Unknown/) would be wrongly
        # skipped, leaving a real date. (audit 2026-07-07)
        return any(s in p.parents for s in subjects)

    def _offset_for(p: Path, own: _dt.datetime) -> _dt.timedelta:
        # Owning subject = a sub-* DIRECTORY component (derivatives survivors live
        # under derivatives/.../sub-X/); exclude the filename p.parts[-1], which
        # can itself start with "sub-". Else anchor the file's own date per-file.
        sub_name = next((x for x in p.parts[:-1] if x.startswith("sub-")), None)
        off = offsets.get(sub_name) if sub_name else None
        if off is None:
            off = _dt.timedelta(days=(own.date() - _DATE_SHIFT_REF).days)
        return off

    for jp in out_root.rglob("*.json"):
        if _already_shifted(jp):
            continue
        data = _strict_load(jp)
        dt = _sidecar_date(data)
        if dt is None:
            continue
        off = _offset_for(jp, dt)
        if off.days == 0:
            continue
        if _shift_sidecar_dates(data, off):
            _save_json(jp, data)
    for sc in out_root.rglob("*_scans.tsv"):
        if _already_shifted(sc):
            continue
        dates = _scans_acq_dates(sc)  # fail-closed: read error raises
        if not dates:
            continue
        off = _offset_for(sc, min(dates))
        if off.days != 0:
            _shift_scans_tsv_dates(sc, off)
    # De-identification is incomplete while provenance survives: .reproin_
    # provenance.tsv AND its rotated .bak carry raw StudyDate/StudyTime (and
    # PatientID under the default -ba o). Both are internal C<->Python
    # intermediates, already consumed by the earlier Unknown-rescue, so remove
    # them (re-runs re-create the .tsv from the DICOMs).
    for name in (_PROVENANCE_TSV, _PROVENANCE_TSV + ".bak"):
        for prov in out_root.rglob(name):
            prov.unlink()
    return shifted


def _run_shift_dates(out_root: Path) -> None:
    """Fail-closed `--shift-dates` invocation shared by both `_post_process`
    exits (the no-sub-* early return AND the normal final pass). Any failure is
    loud and fatal (raises regardless of --strict): an explicit de-identification
    request must never silently ship a tree with partial/real dates."""
    try:
        n = _shift_dates(out_root)
        if n > 0:
            print(f"  shifted acquisition dates to {_DATE_SHIFT_REF.year} "
                  f"for {n} subject(s)", file=sys.stderr)
    except Exception as e:
        print(f"reproinx: ERROR --shift-dates FAILED ({e}); the output may "
              f"contain real acquisition dates — treat it as NOT "
              f"de-identified.", file=sys.stderr)
        raise


def _post_process(out_root: Path, strict: bool, keep_derivatives: bool = False,
                  min_volumes: int = DEFAULT_MIN_VOLUMES,
                  shift_dates: bool = False) -> None:
    # Pre-pass 0: collapse a redundant <StudyDescription> hierarchy when
    # the provenance shows ZERO ReproIn-parsed series. Must run before
    # any rglob-based discovery so subsequent passes see the final paths.
    try:
        if _maybe_collapse_nonreproin_root(out_root, strict):
            print(f"  {out_root}: collapsed redundant non-reproin study hierarchy",
                  file=sys.stderr)
    except Exception as e:
        print(f"reproinx: study-root collapse failed: {e}", file=sys.stderr)
        if strict:
            raise
    # Pre-pass: rescue files from <bids_root>/Unknown/ using JSON BidsGuess
    # plus PatientID/StudyDate/StudyTime from the provenance TSV. Runs for
    # BOTH -f %H and -f %h modes (the latter rarely produces Unknown/ but it
    # can if BidsGuess classification fails entirely). Must run before all
    # other passes so they see the rescued layout. Discovered by rglob so we
    # find Unknown/ even in studies where every series landed there (no
    # sub-* exists yet).
    seen_roots: set[Path] = set()
    for unknown in sorted(out_root.rglob("Unknown")):
        if not unknown.is_dir():
            continue
        if any(part == "derivatives" for part in unknown.parts):
            continue
        root = unknown.parent
        if root in seen_roots:
            continue
        seen_roots.add(root)
        try:
            n = _rescue_unknown_dir(root, strict)
            if n > 0:
                print(f"  {root}: rescued {n} file-stem(s) from Unknown/",
                      file=sys.stderr)
        except Exception as e:
            print(f"reproinx: Unknown/-rescue failed for {root}: {e}",
                  file=sys.stderr)
            if strict:
                raise
        # After rescue: if every remaining Unknown/ file is "discard"
        # (scout / Phoenix / non-image DICOM), drop the whole folder.
        try:
            if _purge_all_discard_unknown(root, strict):
                print(f"  {root}: removed all-discard Unknown/",
                      file=sys.stderr)
        except Exception as e:
            print(f"reproinx: all-discard purge failed for {root}: {e}",
                  file=sys.stderr)
            if strict:
                raise
    # Generic BIDS hygiene pass: discard/ removal, 3D _bold -> _sbref,
    # single-volume DWI to .bidsignore, a/b/c collision files to
    # .bidsignore, residual Unknown/ files to .bidsignore. Each cleanup is
    # no-op when its target is absent.
    _bidsguess_cleanup(out_root, strict, min_volumes)
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
    # Pass 1b: rescue physio recordings from derivatives/scanner/ into the
    # main BIDS tree, stripping the non-spec modality infix from the
    # dcm2niix-emitted filename. Must run after Pass 1 (session backfill)
    # so the destination ses-Y dir exists, and BEFORE Pass 4
    # (_drop_derivatives) which would otherwise discard the source files.
    # Audit 2026-06-11 H1: track skipped physio per root so Pass 4 can
    # refuse to wipe derivatives that still contain un-rescued recordings.
    physio_skipped_per_root: dict[Path, int] = {}
    for root in _bids_roots(_walk_subjects(out_root)):
        try:
            rescued, skipped = _rescue_physio_recordings(root, strict)
            if rescued > 0:
                print(f"  {root}: rescued {rescued} physio file(s) from derivatives/scanner/",
                      file=sys.stderr)
            if skipped > 0:
                physio_skipped_per_root[root] = skipped
        except Exception as e:
            print(f"reproinx: physio rescue failed for {root}: {e}",
                  file=sys.stderr)
            if strict:
                raise
    # Pass 2: per-session scans.tsv + B0Field*.
    sessions = _walk_sessions(out_root)
    if not sessions:
        print(f"reproinx: no sub-* folders under {out_root}", file=sys.stderr)
        # An all-Unknown/ study (CT intentionally left in Unknown/, or a wholly
        # unrescued study) has no sub-* tree, so the session/scaffolding/derivative
        # passes below are no-ops — but --shift-dates must STILL de-identify it:
        # Unknown/*.json carry real AcquisitionDateTime and .reproin_provenance.tsv
        # carries raw StudyDate/StudyTime/PatientID. Without this the pass was
        # skipped entirely, shipping real dates + provenance at exit 0.
        if shift_dates:
            _run_shift_dates(out_root)
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
            np = _ensure_physio_tasknames(root)
            if np > 0:
                print(f"  {root}: backfilled TaskName into {np} physio sidecar(s)",
                      file=sys.stderr)
            ns = _ensure_sbref_tasknames(root)
            if ns > 0:
                print(f"  {root}: backfilled TaskName into {ns} sbref sidecar(s)",
                      file=sys.stderr)
    except Exception as e:
        print(f"reproinx: scaffolding failed: {e}", file=sys.stderr)
        if strict:
            raise
    # Pass 4: drop derivatives/ subtree (default; suppressed by --keep-derivatives).
    # Must run last so earlier passes (session backfill, fmap pairing) can still
    # consult the scout localizer that lives under derivatives/scanner/.
    # Audit 2026-06-11 H1: refuse to drop any root that still has un-rescued
    # physio under derivatives/scanner/ (multi-session subject, destination
    # collision, or rename error). Loud warning + preserved tree on those
    # roots; the rest are cleaned as before. In strict mode the rescue
    # function would have already raised, so the dict is empty.
    if not keep_derivatives:
        if physio_skipped_per_root:
            for root, n in physio_skipped_per_root.items():
                print(f"reproinx: kept derivatives/scanner/ under {root} — "
                      f"{n} un-rescued physio file(s) would be lost. "
                      f"Re-run with --keep-derivatives to acknowledge, or "
                      f"resolve the collision/multi-session blocker.",
                      file=sys.stderr)
        try:
            n = _drop_derivatives(out_root, skip_roots=set(physio_skipped_per_root.keys()))
            if n > 0:
                print(f"  removed derivatives/scanner/ from {n} BIDS root(s)",
                      file=sys.stderr)
        except Exception as e:
            print(f"reproinx: derivatives removal failed: {e}", file=sys.stderr)
            if strict:
                raise
    # Final pass: optional date de-identification (BIDS RECOMMENDED). Runs last
    # so it shifts the finalized scans.tsv + sidecars in the raw tree.
    if shift_dates:
        _run_shift_dates(out_root)


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
    p.add_argument("--min-volumes", "-N", type=int, default=DEFAULT_MIN_VOLUMES,
                   metavar="N",
                   help="Cross-series classification threshold (default "
                        f"{DEFAULT_MIN_VOLUMES}). A timeseries with fewer than N "
                        "volumes that sits beside a long (>=N) EPI is treated as "
                        "a PEPOLAR distortion map (fmap/_dir-<L>_epi) rather than "
                        "func/dwi data.")
    p.add_argument("--keep-derivatives", action="store_true",
                   help="Keep the `derivatives/scanner/` subtree under each "
                        "study root after post-processing. dcm2niix routes "
                        "scouts and DERIVED-flagged images (FA, ColFA, "
                        "TENSOR_B0, physio) there; they are consumed for "
                        "session detection and then removed by default to "
                        "match heudiconv's layout. Pass this flag to retain "
                        "them for inspection.")
    p.add_argument("--shift-dates", action="store_true",
                   help="De-identify acquisition dates (BIDS RECOMMENDED). Per "
                        "subject, shift every acq_time / AcquisitionDateTime by a "
                        "whole-day offset so the earliest scan lands on "
                        f"{_DATE_SHIFT_REF.year}-01-01, preserving time-of-day and "
                        "intra-subject intervals. Off by default (heudiconv "
                        "parity keeps real dates). Does not rename timestamp-"
                        "based ses-<YYYYMMDD...> labels (warns loudly when any "
                        "are present, since the path still leaks the date).")
    args = p.parse_args(argv)

    indir = Path(args.indir).resolve()
    outdir = Path(args.outdir).resolve() if args.outdir else indir

    if not indir.is_dir():
        print(f"reproinx: indir does not exist: {indir}", file=sys.stderr)
        return 2
    outdir.mkdir(parents=True, exist_ok=True)

    dcm2niix_rc = 0
    if not args.no_convert:
        dcm2niix_rc = _run_dcm2niix(str(indir), str(outdir), args.subject,
                                     args.session, anonymize=args.anonymize)

    try:
        _post_process(outdir, strict=args.strict,
                      keep_derivatives=args.keep_derivatives,
                      min_volumes=args.min_volumes,
                      shift_dates=args.shift_dates)
    except Exception as e:
        # Fail-closed surface for --shift-dates (and --strict): a clean non-zero
        # exit instead of a raw traceback. The pass already printed specifics.
        print(f"reproinx: post-processing aborted: {e}", file=sys.stderr)
        return 1
    # Propagate dcm2niix's partial-success exit codes (8, 10) so automation
    # can distinguish a clean run from one that lost series mid-batch. The
    # post-pass still runs against whatever landed on disk.
    return dcm2niix_rc


if __name__ == "__main__":
    raise SystemExit(main())
