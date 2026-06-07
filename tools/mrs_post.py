#!/usr/bin/env python3
"""mrs_post.py — vendor-state post-processor for dcm2niix MRS outputs.

dcm2niix's C-side MRS writer bundles raw multi-DICOM / multi-frame
spectroscopy series into a single NIfTI (`dim[5] = total_frames`, no
reorder, no drop). This tool consumes the bundled output PLUS the
source DICOMs and applies the vendor-sequence-state-specific split /
crop / reshape that spec2nii bakes in. The C/Python split was decided
2026-06-07 to keep vendor Phoenix-Protocol / per-frame private-tag
interpretation above the converter (see CLAUDE.md "MRS split policy").

Three cases handled:

1. CMRR sLASER DKD multi-DICOM (Siemens VB/VE) — port of spec2nii's
   identify_integrated_references. Reads Phoenix Protocol from the
   first source DICOM (lAutoRefScanMode, lAutoRefScanNo, lAverages,
   tSequenceFileName), classifies each frame by InstanceNumber, and
   writes per-group NIfTIs with the canonical spec2nii suffixes
   (_rf_off, _rf_grads_ovs_off, etc.).

2. Philips MEGA-PRESS (single Enhanced DICOM, multiple frames) — port
   of spec2nii's _process_philips_svs_new MEGA branch. Reads per-frame
   private tags (2005,1304) (ref flag) and (2005,1598) (edit ON/OFF),
   reshapes the bundled (N_pts, N_frames) into a paired
   (N_pts, n_dyn, 2)_svs (edit ON/OFF on axis 2) + (N_pts, n_ref)_mrsref.

3. _mrsref companion sanity — pass-through validation for Philips
   classic 2× cases where dcm2niix's C-side companion writer already
   emitted both _svs and _mrsref.

Dependencies: pydicom (already a reproinx.py dep) + numpy. Pure-stdlib
otherwise.

Attribution: split logic ported from spec2nii (BSD-3-Clause, William
Clarke, U. Oxford 2020). See spec2nii/Siemens/dicomfunctions.py:737-849
(identify_integrated_references) and spec2nii/Philips/philips_dcm.py:
209-258 (_process_philips_svs_new MEGA branch).
"""

import argparse
import gzip
import json
import os
import re
import struct
import sys

try:
    import numpy as np
except ImportError:
    sys.exit("mrs_post.py requires numpy; install with `pip install numpy`")

try:
    import pydicom
except ImportError:
    sys.exit("mrs_post.py requires pydicom; install with `pip install pydicom`")


# ---------------------------------------------------------------------------
# NIfTI-1 I/O — minimal helpers, complex64 only (BIDS-MRS).
# ---------------------------------------------------------------------------

NIFTI1_HEADER_SIZE = 348
NIFTI1_EXT_BYTES = 4  # zero ext header that follows the main 348 bytes


def _open_nii(path):
    if path.endswith(".gz"):
        return gzip.open(path, "rb")
    return open(path, "rb")


def read_nifti_complex64(path):
    """Return (header_bytes, complex_data_ndarray, shape)."""
    with _open_nii(path) as f:
        hdr = f.read(NIFTI1_HEADER_SIZE)
        if len(hdr) != NIFTI1_HEADER_SIZE:
            raise IOError(f"Truncated NIfTI header: {path}")
        # vox_offset at byte 108, float32
        vox_offset = int(struct.unpack_from("<f", hdr, 108)[0])
        datatype = struct.unpack_from("<h", hdr, 70)[0]
        if datatype != 32:  # NIFTI_TYPE_COMPLEX64
            raise ValueError(
                f"Expected complex64 (datatype=32), got {datatype} in {path}"
            )
        dim = struct.unpack_from("<8h", hdr, 40)
        ndim = dim[0]
        shape = tuple(dim[1 : ndim + 1])
        # Skip to vox_offset
        f.seek(vox_offset)
        nelem = 1
        for d in shape:
            nelem *= d
        raw = f.read(nelem * 8)  # 8 bytes per complex64 (2 floats)
        if len(raw) != nelem * 8:
            raise IOError(
                f"NIfTI payload short read: got {len(raw)} bytes, expected {nelem * 8}"
            )
        arr = np.frombuffer(raw, dtype=np.complex64).copy()
        arr = arr.reshape(shape, order="F")  # NIfTI is Fortran/column-major
    return hdr, arr, shape


def write_nifti_complex64(out_path, ref_header, new_data):
    """Write a complex64 NIfTI, reusing the reference header but updating dims.

    new_data: complex64 ndarray with shape (1,1,1, N_pts, ...).
    """
    if new_data.dtype != np.complex64:
        new_data = new_data.astype(np.complex64)
    hdr = bytearray(ref_header)
    shape = new_data.shape
    ndim = len(shape)
    # Pad/truncate to 7 dims
    full = list(shape) + [1] * (7 - ndim)
    full = full[:7]
    struct.pack_into("<h", hdr, 40, ndim)
    for i, sz in enumerate(full):
        if sz > 32767:
            raise ValueError(f"dim[{i+1}]={sz} exceeds NIfTI-1 int16 limit")
        struct.pack_into("<h", hdr, 42 + 2 * i, sz)
    # vox_offset >= 352 (header + 4-byte zero ext)
    struct.pack_into("<f", hdr, 108, 352.0)
    payload_bytes = new_data.tobytes(order="F")
    is_gz = out_path.endswith(".gz")
    body = bytes(hdr) + b"\x00\x00\x00\x00" + payload_bytes
    if is_gz:
        with gzip.open(out_path, "wb") as f:
            f.write(body)
    else:
        with open(out_path, "wb") as f:
            f.write(body)


# ---------------------------------------------------------------------------
# Sidecar I/O
# ---------------------------------------------------------------------------


def read_sidecar(path):
    with open(path) as f:
        return json.load(f)


def write_sidecar(path, obj):
    with open(path, "w") as f:
        json.dump(obj, f, indent="\t")
        f.write("\n")


# ---------------------------------------------------------------------------
# Phoenix Protocol extraction (Siemens VB/VE CSA)
# ---------------------------------------------------------------------------

# Phoenix lives in CSA Series Header at one of the legacy Siemens private tags.
# (0029,1020) is the classic CSA Series Header; (0029,1120) is an alias seen
# in some VB/VE classic SVS DICOMs; we search both. The CSA payload is an
# opaque binary blob; we just grep for the ASCCONV BEGIN/END markers.
PHOENIX_TAGS = [(0x0029, 0x1120), (0x0029, 0x1020)]
ASCCONV_RE = re.compile(rb"### ASCCONV BEGIN.*?### ASCCONV END ###", re.DOTALL)
KEY_RE = re.compile(r"^\s*([^=\s]+)\s*=\s*(.+?)\s*$")


def _extract_phoenix(ds):
    """Return the ASCCONV text (str, latin-1 decoded) or None."""
    for tag in PHOENIX_TAGS:
        elem = ds.get(tag)
        if elem is None or elem.value is None:
            continue
        b = elem.value if isinstance(elem.value, bytes) else bytes(elem.value)
        m = ASCCONV_RE.search(b)
        if m:
            return m.group(0).decode("latin-1", errors="replace")
    # Fallback: walk all OB elements > 1 KB
    for elem in ds.iterall():
        try:
            if elem.VR == "OB" and elem.value and len(elem.value) > 1000:
                b = (
                    elem.value
                    if isinstance(elem.value, bytes)
                    else bytes(elem.value)
                )
                m = ASCCONV_RE.search(b)
                if m:
                    return m.group(0).decode("latin-1", errors="replace")
        except Exception:
            continue
    return None


def _parse_phoenix(text):
    """Parse ASCCONV text into a dict. Strings keep surrounding quotes."""
    out = {}
    for line in text.split("\n"):
        m = KEY_RE.match(line)
        if not m:
            continue
        out[m.group(1).strip()] = m.group(2).strip()
    return out


def _phoenix_float(d, key, default=None):
    v = d.get(key)
    if v is None:
        return default
    try:
        return float(v)
    except ValueError:
        return default


def _phoenix_str(d, key, default=""):
    v = d.get(key, default)
    return v.strip('"') if isinstance(v, str) else default


# ---------------------------------------------------------------------------
# Case 1 — Siemens sLASER DKD multi-DICOM split.
# ---------------------------------------------------------------------------


def _identify_dkd_reference(seq_name, mode, num_ref, num_dyn, inst_num):
    """Port of spec2nii.identify_integrated_references for DKD sLASER.

    Returns (group_int, suffix_str). group=0 means main; >0 means a reference
    group that gets its own suffix.
    """
    # svs_slaser_dkd or svs_slaserVOI_dkd at mode=8 (4-and-4 reference layout)
    if (
        re.search(r"svs_slaser(voi)?_dkd2$", seq_name) and mode == 8
    ) or (
        re.search(r"svs_slaser(voi)?_dkd$", seq_name) and mode == 8
    ):
        total_dyn = num_dyn + (num_ref * 4)
        if inst_num <= num_ref:
            return 1, "_rf_off"
        if inst_num <= num_ref * 2:
            return 2, "_rf_grads_ovs_off"
        if (total_dyn - 2 * num_ref) < inst_num <= (total_dyn - num_ref):
            return 1, "_rf_off"
        if (total_dyn - num_ref) < inst_num <= total_dyn:
            return 2, "_rf_grads_ovs_off"
        return 0, ""
    # svs_slaserVOI_dkd2 at mode=2 (refs at start AND end). Mirror spec2nii's
    # bounds exactly: `inst_num < num_ref` for the start refs and
    # `inst_num >= (total_dyn - num_ref)` for the end refs. These are
    # spec2nii's actual comparisons (see dicomfunctions.py:802-807); any
    # off-by-one here breaks FID byte-parity.
    if (
        re.search(r"svs_slaser(voi)?_dkd2", seq_name) and mode == 2
    ):
        total_dyn = num_dyn + (num_ref * 2)
        if inst_num < num_ref:
            return 1, "_vapor_ovs_rfoff"
        if inst_num >= (total_dyn - num_ref):
            return 1, "_vapor_ovs_rfoff"
        return 0, ""
    return 0, ""


def split_siemens_dkd(nii_path, json_path, dicom_paths, verbose=False):
    """Split a bundled Siemens sLASER DKD multi-DICOM SVS into ref groups.

    Returns list of (out_basename, group_label) for each written NIfTI.
    """
    # Read all source DICOMs to get InstanceNumber per frame.
    instances = []
    for fn in dicom_paths:
        try:
            ds = pydicom.dcmread(fn, stop_before_pixels=False, force=True)
        except Exception as exc:
            if verbose:
                print(f"  skip {fn}: {exc}")
            continue
        inst_num = int(getattr(ds, "InstanceNumber", 0) or 0)
        instances.append((inst_num, fn, ds))
    if not instances:
        return []
    # Sort by InstanceNumber so the result frame order matches dcm2niix's
    # ascending sort.
    instances.sort(key=lambda t: t[0])

    # Extract Phoenix from the first DICOM (all share the same Phoenix Protocol).
    phoenix_text = _extract_phoenix(instances[0][2])
    if phoenix_text is None:
        if verbose:
            print("  no Phoenix Protocol; falling back to no-split")
        return []
    pp = _parse_phoenix(phoenix_text)

    seq_name = _phoenix_str(pp, "tSequenceFileName").lower()
    # Strip the %CustomerSeq%\ prefix
    seq_name = seq_name.split("\\")[-1] if "\\" in seq_name else seq_name
    mode = _phoenix_float(pp, "sSpecPara.lAutoRefScanMode")
    num_ref = _phoenix_float(pp, "sSpecPara.lAutoRefScanNo")
    num_dyn = _phoenix_float(pp, "lAverages")
    if mode is None or num_ref is None or num_dyn is None:
        if verbose:
            print(
                f"  missing Phoenix keys: mode={mode}, num_ref={num_ref}, num_dyn={num_dyn}"
            )
        return []
    mode = int(mode)
    num_ref = int(num_ref)
    num_dyn = int(num_dyn)

    # Classify each frame.
    classes = []
    for inst_num, fn, ds in instances:
        g, suffix = _identify_dkd_reference(
            seq_name, mode, num_ref, num_dyn, inst_num
        )
        classes.append((g, suffix, fn))
    if all(g == 0 for g, _, _ in classes):
        if verbose:
            print("  every frame classified as main; no split needed")
        return []

    # Read the bundled NIfTI.
    hdr, data, shape = read_nifti_complex64(nii_path)
    n_frames = shape[4] if len(shape) >= 5 else 1
    if n_frames != len(instances):
        # dcm2niix should bundle one frame per DICOM in this case. If not, bail.
        if verbose:
            print(
                f"  frame count mismatch: NIfTI dim[5]={n_frames} vs n_dicoms={len(instances)}"
            )
        return []

    sidecar = read_sidecar(json_path)

    # Group frames by their classification (preserving InstanceNumber order).
    groups = {}  # suffix -> [frame indices]
    for idx, (g, suffix, fn) in enumerate(classes):
        groups.setdefault(suffix, []).append(idx)

    base = re.sub(r"_svs(\.nii(\.gz)?)?$", "", nii_path).replace(".nii.gz", "").replace(".nii", "")
    is_gz = nii_path.endswith(".gz")
    ext = ".nii.gz" if is_gz else ".nii"

    written = []
    main_suffix = sidecar.get("BidsGuess", ["", "_svs"])[1] if len(sidecar.get("BidsGuess", [])) > 1 else "_svs"
    for suffix, indices in groups.items():
        sub = data[..., indices] if data.ndim == 5 else data[:, :, :, :, indices, ...]
        if sub.ndim < 5:
            sub = sub.reshape((1, 1, 1, sub.shape[-1], len(indices)))
        # Filename gets the spec2nii group suffix; BidsGuess stays canonical
        # so the comparator's BidsGuess-match picks the main as `_svs` and
        # the ref groups (water-suppression off) get `_mrsref`.
        out_suffix_fname = main_suffix + suffix if suffix else main_suffix
        out_base = base + out_suffix_fname
        out_nii = out_base + ext
        out_json = out_base + ".json"
        write_nifti_complex64(out_nii, hdr, sub)
        sc = dict(sidecar)
        if "NumberOfAverages" in sc:
            try:
                sc["NumberOfTransients"] = int(sc["NumberOfAverages"]) * len(indices)
            except Exception:
                pass
        if suffix:
            sc["WaterSuppressed"] = False
            sc["BidsGuess"] = ["mrs", "_mrsref"]
            sc["MrsPostGroup"] = suffix.lstrip("_")
        write_sidecar(out_json, sc)
        written.append((out_base, suffix or "main"))
        if verbose:
            print(
                f"  wrote {out_nii}  (suffix={suffix or 'main'}, {len(indices)} frames)"
            )
    return written


# ---------------------------------------------------------------------------
# Case 2 — Philips MEGA-PRESS reshape.
# ---------------------------------------------------------------------------


def reshape_philips_mega(nii_path, json_path, dicom_path, verbose=False):
    """Reshape a Philips MEGA-PRESS bundled SVS into (N_pts, n_dyn, 2) + _mrsref.

    Returns list of (out_basename, group_label) for each written NIfTI.
    """
    ds = pydicom.dcmread(dicom_path, stop_before_pixels=True, force=True)
    # Check this is actually MEGA (private tag (2005,1597)=='Y' for is_edited).
    try:
        is_edited = ds[0x2005, 0x1597].value == "Y"
    except KeyError:
        is_edited = False
    if not is_edited:
        if verbose:
            print("  not a MEGA-edited series; skip")
        return []
    # Walk per-frame functional groups (2005,140f) for (2005,1304) + (2005,1598).
    try:
        pfg = ds[0x2005, 0x140F]
    except KeyError:
        if verbose:
            print("  no per-frame functional groups; skip")
        return []
    on_idx, off_idx, ref_idx = [], [], []
    for idx, instance in enumerate(pfg):
        try:
            ref_flag = instance[0x2005, 0x1304].value
        except KeyError:
            ref_flag = 0
        if ref_flag:
            ref_idx.append(idx)
            continue
        try:
            edit_flag = instance[0x2005, 0x1598].value
        except KeyError:
            continue
        if edit_flag == "1":
            on_idx.append(idx)
        elif edit_flag == "0":
            off_idx.append(idx)
    if not on_idx or len(on_idx) != len(off_idx):
        if verbose:
            print(
                f"  edit-on/off counts don't pair: on={len(on_idx)}, off={len(off_idx)}; skip"
            )
        return []

    hdr, data, shape = read_nifti_complex64(nii_path)
    # bundled shape is (1,1,1, N_pts, N_frames). Subset on dim[5].
    if data.ndim < 5:
        if verbose:
            print(f"  unexpected NIfTI ndim={data.ndim}; skip")
        return []
    on_data = data[..., on_idx]
    off_data = data[..., off_idx]
    n_pts = on_data.shape[3]
    n_dyn = on_data.shape[4]
    # Stack ON/OFF on a new dim[6] axis: (1,1,1, N_pts, n_dyn, 2).
    main_arr = np.stack([on_data, off_data], axis=-1)
    sidecar = read_sidecar(json_path)
    base = re.sub(r"_svs(\.nii(\.gz)?)?$", "", nii_path).replace(".nii.gz", "").replace(".nii", "")
    is_gz = nii_path.endswith(".gz")
    ext = ".nii.gz" if is_gz else ".nii"
    main_base = base + "_svs"
    write_nifti_complex64(main_base + ext, hdr, main_arr)
    sc_main = dict(sidecar)
    sc_main["dim_5"] = "DIM_DYN"
    sc_main["dim_6"] = "DIM_EDIT"
    sc_main["dim_6_header"] = {"EditCondition": ["ON", "OFF"]}
    sc_main["NumberOfTransients"] = n_dyn
    sc_main["BidsGuess"] = ["mrs", "_svs"]
    write_sidecar(main_base + ".json", sc_main)
    written = [(main_base, "main_edit_on_off")]
    if verbose:
        print(
            f"  wrote {main_base + ext}  ({n_pts} pts, {n_dyn} dyn, 2 edit ON/OFF)"
        )
    # Write _mrsref companion if there were ref frames.
    if ref_idx:
        ref_data = data[..., ref_idx]
        ref_base = base + "_mrsref"
        write_nifti_complex64(ref_base + ext, hdr, ref_data)
        sc_ref = dict(sidecar)
        sc_ref["dim_5"] = "DIM_DYN"
        sc_ref["WaterSuppressed"] = False
        sc_ref["NumberOfTransients"] = len(ref_idx)
        sc_ref["BidsGuess"] = ["mrs", "_mrsref"]
        write_sidecar(ref_base + ".json", sc_ref)
        written.append((ref_base, "mrsref"))
        if verbose:
            print(f"  wrote {ref_base + ext}  ({len(ref_idx)} ref frames)")
    return written


# ---------------------------------------------------------------------------
# Case 3 — _mrsref companion sanity (Philips classic 2×).
# ---------------------------------------------------------------------------


def check_mrsref_companion(nii_path, json_path, verbose=False):
    """Validate that a _svs NIfTI has a paired _mrsref companion on disk.

    Returns [(svs_base, 'main'), (mrsref_base, 'mrsref')] if both exist; else
    just the svs entry.
    """
    base = re.sub(r"_svs(\.nii(\.gz)?)?$", "", nii_path).replace(".nii.gz", "").replace(".nii", "")
    is_gz = nii_path.endswith(".gz")
    ext = ".nii.gz" if is_gz else ".nii"
    mrsref_nii = base + "_mrsref" + ext
    mrsref_json = base + "_mrsref.json"
    written = [(base + "_svs", "main")]
    if os.path.exists(mrsref_nii) and os.path.exists(mrsref_json):
        if verbose:
            print(f"  validated _mrsref companion: {mrsref_nii}")
        written.append((base + "_mrsref", "mrsref"))
    return written


# ---------------------------------------------------------------------------
# Dispatch.
# ---------------------------------------------------------------------------


def _filter_dicoms_by_series(dicom_paths, target_series_num, verbose=False):
    """Return only the DICOMs whose SeriesNumber matches target_series_num."""
    if target_series_num is None:
        return dicom_paths
    matched = []
    for fn in dicom_paths:
        try:
            ds = pydicom.dcmread(fn, stop_before_pixels=True, force=True)
            sn = getattr(ds, "SeriesNumber", None)
            if sn is not None and int(sn) == int(target_series_num):
                matched.append(fn)
        except Exception:
            continue
    return matched if matched else dicom_paths


def detect_case(nii_path, json_path, dicom_dir, verbose=False):
    """Return one of: 'siemens_dkd', 'philips_mega', 'mrsref_check', None."""
    sc = read_sidecar(json_path)
    mfg = sc.get("Manufacturer", "").lower()
    target_sn = sc.get("SeriesNumber")
    if dicom_dir is None:
        return None
    dicom_paths = []
    if os.path.isdir(dicom_dir):
        for root, _, files in os.walk(dicom_dir):
            for fn in sorted(files):
                full = os.path.join(root, fn)
                if fn.endswith((".json", ".nii", ".nii.gz", ".tsv", ".bval", ".bvec")):
                    continue
                dicom_paths.append(full)
    elif os.path.isfile(dicom_dir):
        dicom_paths = [dicom_dir]
    if not dicom_paths:
        return None
    # Filter to just this series — handles the press_mega case where the
    # corpus directory holds both SV_MEGA and SV_PRESS reference series.
    dicom_paths = _filter_dicoms_by_series(dicom_paths, target_sn, verbose=verbose)
    if verbose:
        print(f"  {len(dicom_paths)} DICOM(s) matched series {target_sn}")
    # Siemens sLASER DKD multi-DICOM: many files, Siemens vendor, sLASER seq.
    if "siemens" in mfg and len(dicom_paths) > 1:
        try:
            ds = pydicom.dcmread(dicom_paths[0], stop_before_pixels=True, force=True)
            phoenix = _extract_phoenix(ds)
            if phoenix and "slaser" in phoenix.lower():
                return ("siemens_dkd", dicom_paths)
        except Exception:
            pass
    # Philips MEGA: single Enhanced DICOM, edited.
    if "philips" in mfg and len(dicom_paths) == 1:
        try:
            ds = pydicom.dcmread(dicom_paths[0], stop_before_pixels=True, force=True)
            if ds.get((0x2005, 0x1597)) is not None and ds[0x2005, 0x1597].value == "Y":
                return ("philips_mega", dicom_paths[0])
        except Exception:
            pass
    return ("mrsref_check", None)


def main():
    ap = argparse.ArgumentParser(
        description=(
            "Post-process dcm2niix MRS outputs: split CMRR sLASER DKD "
            "multi-DICOM reference groups, reshape Philips MEGA-PRESS "
            "edit ON/OFF, validate _mrsref companions."
        )
    )
    ap.add_argument(
        "nifti",
        help="Path to the bundled _svs NIfTI emitted by dcm2niix (.nii or .nii.gz)",
    )
    ap.add_argument(
        "--dicoms",
        required=True,
        help="Path to the source DICOM directory (or single file for Philips MEGA)",
    )
    ap.add_argument(
        "--verbose", "-v", action="store_true", help="Verbose progress output"
    )
    args = ap.parse_args()

    nii = args.nifti
    if not os.path.exists(nii):
        sys.exit(f"NIfTI not found: {nii}")
    json_path = nii.replace(".nii.gz", ".json").replace(".nii", ".json")
    if not os.path.exists(json_path):
        sys.exit(f"Sidecar not found: {json_path}")

    case = detect_case(nii, json_path, args.dicoms, verbose=args.verbose)
    if case is None:
        if args.verbose:
            print("No applicable post-processing case detected; nothing to do.")
        return 0
    kind = case[0]
    if args.verbose:
        print(f"Case: {kind}")
    if kind == "siemens_dkd":
        _, dicom_paths = case
        written = split_siemens_dkd(nii, json_path, dicom_paths, verbose=args.verbose)
    elif kind == "philips_mega":
        _, dicom_path = case
        written = reshape_philips_mega(nii, json_path, dicom_path, verbose=args.verbose)
    elif kind == "mrsref_check":
        written = check_mrsref_companion(nii, json_path, verbose=args.verbose)
    else:
        written = []
    if args.verbose:
        for base, label in written:
            print(f"  -> {base} ({label})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
