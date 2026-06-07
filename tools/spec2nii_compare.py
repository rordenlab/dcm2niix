#!/usr/bin/env python3
"""Compare spec2nii vs dcm2niix DICOM-MRS conversion.

Phase 0 tooling for the spec2nii ↔ dcm2niix DICOM-MRS parity workplan
(see spec_plan.md). Runs both converters on the same source DICOM(s),
parses both NIfTI headers + FID payloads in stdlib, and reports:

- FID byte-identical (after endian + datatype normalisation)
- sform match to float32 (1e-4 absolute, 1e-5 relative)
- dim/pixdim match
- sidecar JSON field-by-field diff (with a versioned ignore list)

spec2nii emits NIfTI-2 (sizeof_hdr=540) + an in-NIfTI metadata extension
+ an optional -j JSON sidecar. dcm2niix emits NIfTI-1 (sizeof_hdr=348)
+ a BIDS JSON sidecar. We compare the FID payloads (datatype DT_COMPLEX64,
identical layout in both formats), the sform_xyz columns (rounded to
float32), and the side-by-side JSON sidecars.

Usage:
    python tools/spec2nii_compare.py <dataset_id>     # compare single dataset
    python tools/spec2nii_compare.py --all            # walk the inventory, report per-dataset
    python tools/spec2nii_compare.py --list           # print the dataset inventory

Vendor dispatch is encoded in DATASETS below; each entry knows which
spec2nii subcommand to call. Per-dataset overrides for ignored sidecar
fields are also embedded there.
"""

from __future__ import annotations

import argparse
import gzip
import json
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import NamedTuple

REPO = Path(__file__).resolve().parent.parent
SPEC2NII_DATA = Path("/Users/chris/src/spec2nii/tests/spec2nii_test_data")
DCM2NIIX_BIN = REPO / "build" / "bin" / "dcm2niix"

# Fields the parity diff considers "informational" — present on one side or
# the other but not a parity failure. Tag-by-tag rationale:
#
# spec2nii's PII / provenance-only fields (we never emit these; they're not
# BIDS-MRS and they're identifying data the C side intentionally drops):
SPEC2NII_PII_FIELDS = {
    "ConversionMethod", "ConversionTime", "OriginalFile",
    "PatientDoB", "PatientID", "PatientName", "PatientSex", "PatientWeight",
    "kSpace",   # spec2nii dumps an internal flag; not BIDS-MRS required
    "PulseSequenceFile",  # spec2nii provenance, not BIDS-MRS
}

# dcm2niix's wide DICOM-provenance sidecar (we emit these by design per Q4 —
# the BIDS-MRS spec doesn't forbid them and they're useful for downstream
# tools):
DCM2NIIX_WIDE_FIELDS = {
    "ConversionSoftware", "ConversionSoftwareVersion", "BidsGuess",
    # Identification / institution
    "AcquisitionDuration", "AcquisitionMatrixPE", "AcquisitionNumber",
    "AcquisitionTime", "BaseResolution", "BodyPart",
    "CoilCombinationMethod", "CoilString", "ConsistencyInfo",
    "DeviceSerialNumber", "InstitutionAddress", "InstitutionalDepartmentName",
    "InstitutionName", "Manufacturer", "ManufacturersModelName",
    "MagneticFieldStrength", "MatrixCoilMode", "Modality",
    "ProcedureStepDescription", "ProtocolName", "RawImage",
    "ReceiveCoilActiveElements", "ReceiveCoilName", "ScanOptions",
    "SeriesDescription", "SeriesNumber", "ShimSetting", "SoftwareVersions",
    "StationName", "StudyDescription", "TxRefAmp",
    # Decay / frame / array fields that are large repeating zeros
    "DecayCorrectionFactor", "FrameDuration", "FrameTimesStart",
    "FrameReferenceTime",
    # Imaging-side metadata not BIDS-MRS-required
    "ImageComments", "ImageOrientationPatientDICOM", "ImageType",
    "ImageTypeText", "ImagingFrequency", "MRSpectroscopyAcquisitionType",
    "NonlinearGradientCorrection", "NumberOfAverages",
    "NumberOfKSpaceTrajectories", "ParallelReductionFactorInPlane",
    "ParallelReductionFactorOutOfPlane", "PercentPhaseFOV", "PercentSampling",
    "PhaseResolution", "PulseSequenceDetails", "PulseSequenceName",
    "SequenceName", "ScanningSequence", "SequenceVariant", "SpoilingState",
    "TablePosition",
}

# BIDS-MRS field name aliases. spec2nii uses some names that don't quite match
# the published BIDS-MRS naming; dcm2niix follows the BIDS-MRS spec. Equivalence
# pairs (spec2nii_name -> dcm2niix_name) — both populated, both correct, only
# names differ. These are informational, not parity bugs (we keep our names).
BIDS_MRS_ALIASES = {
    # spec2nii calls FlipAngle "ExcitationFlipAngle"; BIDS spec uses FlipAngle
    "ExcitationFlipAngle": "FlipAngle",
    # spec2nii uses RxCoil/TxCoil; BIDS spec uses ReceiveCoilName/TransmitCoilName
    "RxCoil": "ReceiveCoilName",
    "TxCoil": "TransmitCoilName",
}

IGNORE_FIELDS_GLOBAL = SPEC2NII_PII_FIELDS | DCM2NIIX_WIDE_FIELDS


class Dataset(NamedTuple):
    id: str
    vendor: str                # "siemens" | "philips" | "uih"
    spec2nii_cmd: str          # "dicom" | "philips_dcm" | "uih"
    source: Path
    bids_suffix: str           # "_svs" | "_mrsi" | "_mrsref" | "_unloc"
    notes: str = ""


def _ds(vendor: str, spec2nii_cmd: str, relpath: str, bids_suffix: str = "_svs",
        notes: str = "", id_override: str | None = None) -> Dataset:
    src = SPEC2NII_DATA / relpath
    # Default id: stem of the source's deepest meaningful dir-or-file name.
    # If the source is a file in a numbered-frame dir (e.g. UIH 00000001.dcm),
    # use the parent dir name instead so siblings don't collide.
    if id_override:
        ds_id = id_override
    else:
        anchor = src.parent.name if (src.is_file() and src.stem.lstrip("0").isdigit()) else src.stem
        ds_id = f"{vendor}_{anchor}".replace(">", "_gt_")
    return Dataset(id=ds_id, vendor=vendor, spec2nii_cmd=spec2nii_cmd,
                   source=src, bids_suffix=bids_suffix, notes=notes)


# Phase 1-3 corpus (Phase 4 datasets get added when MRSI lands).
DATASETS = [
    # ---- Siemens SVS (Phase 1) ----
    _ds("siemens", "dicom", "Siemens/VBData/DICOM/svs_se_C>T15>S10_10_12_1",
        notes="VB-line SVS (NumarisX pre-XA, real+1j*imag phase)"),
    _ds("siemens", "dicom", "Siemens/VEData/DICOM/svs_se_c>t15>s10_R10_12_1",
        notes="VE-line SVS, R10 (reverse readout)"),
    _ds("siemens", "dicom", "Siemens/XAData/XA20/DICOM/26516628.dcm",
        notes="XA20 single-DICOM SVS"),
    _ds("siemens", "dicom", "Siemens/XAData/XA30/meas_MID00479_FID106847_svs_se_135sws.dcm",
        notes="XA30 single-DICOM SVS (135-ms TE water-suppressed)"),
    _ds("siemens", "dicom", "Siemens/anon/anon_dcm.IMA",
        notes="Anonymised SVS"),
    # sLASER family (DICOM/ subfolder; the corpus also has a twix/ folder that's out of scope)
    _ds("siemens", "dicom", "Siemens/special_cases_slaser_dkd/DICOM/svs_slaser_dkd_von_wrs1_15_None",
        notes="sLASER WRS1"),
    _ds("siemens", "dicom", "Siemens/special_cases_slaser_dkd/DICOM/svs_slaser_dkd_von_wrs2_17_None",
        notes="sLASER WRS2"),
    _ds("siemens", "dicom", "Siemens/special_cases_slaser_dkd/DICOM/svs_slaser_dkd_von_wrsoff_13_None",
        bids_suffix="_mrsref",
        notes="sLASER WRS off — _mrsref candidate (water reference)"),
    _ds("siemens", "dicom", "Siemens/special_cases_slaser_dkd/DICOM/svs_slaserVOI_dkd2_von_wrsw1pw3_1_21_None",
        notes="sLASER VOI WRSw1pw3"),
    _ds("siemens", "dicom", "Siemens/special_cases_slaser_dkd/DICOM/svs_slaserVOI_dkd2_von_wrsw1pw3_2_23_None",
        notes="sLASER VOI WRSw1pw3 (rep2)"),
    _ds("siemens", "dicom", "Siemens/special_cases_slaser_dkd/DICOM/svs_slaserVOI_dkd2_von_wrsw4_1_25_None",
        notes="sLASER VOI WRSw4"),
    _ds("siemens", "dicom", "Siemens/special_cases_slaser_dkd/DICOM/svs_slaserVOI_dkd2_von_wrsw4_2_27_None",
        notes="sLASER VOI WRSw4 (rep2)"),
    _ds("siemens", "dicom", "Siemens/special_cases_slaser_dkd/DICOM/svs_slaserVOI_dkd2_von_wrsoff_19_None",
        bids_suffix="_mrsref",
        notes="sLASER VOI WRS off — _mrsref candidate"),
    # voi_in_mrsi — has the IMA reference (MRSI with VOI mask)
    _ds("siemens", "dicom",
        "Siemens/voi_in_mrsi/F3T_2021_PH_016.MR.FMRIB_DEVELOPER_WILL.0004.0001.2021.07.01.16.43.12.374485.667204044.IMA",
        bids_suffix="_mrsi", notes="MRSI with companion VOI mask"),
    # HERCULES / hyper_isthmus / fid: corpus ships .dat (Twix) only — out of
    # scope for DICOM parity. tracked here as a note so we don't re-add them.

    # ---- Siemens MRSI / CSI (Phase 4 — placeholders for now) ----
    _ds("siemens", "dicom", "Siemens/VBData/DICOM/csi_se_3D_C>S23.5>T20.3_10_8_1",
        bids_suffix="_mrsi", notes="VB-line 3D CSI"),
    _ds("siemens", "dicom", "Siemens/VEData/DICOM/csi_se_3D_c>s23.5>t20.3_R10_9_1",
        bids_suffix="_mrsi", notes="VE-line 3D CSI"),
    _ds("siemens", "dicom", "Siemens/enhanced_dcm_csi/sm_classic",
        bids_suffix="_mrsi", notes="Enhanced CSI (Classic SOP)"),
    _ds("siemens", "dicom", "Siemens/enhanced_dcm_csi/sm_enhanced",
        bids_suffix="_mrsi", notes="Enhanced CSI (Enhanced SOP)"),
    _ds("siemens", "dicom", "Siemens/enhanced_dcm_csi/rk_enhanced",
        bids_suffix="_mrsi", notes="Enhanced CSI variant"),

    # ---- Philips (Phase 2) ----
    _ds("philips", "philips_dcm", "philips/DICOM/SV_phantom_center",
        notes="Classic SVS, centred"),
    _ds("philips", "philips_dcm", "philips/DICOM/SV_phantom_H15mm",
        notes="Classic SVS, H15mm offset"),
    _ds("philips", "philips_dcm", "philips/DICOM/SV_phantom_R15mm",
        notes="Classic SVS, R15mm offset"),
    _ds("philips", "philips_dcm", "philips/DICOM/SV_phantom_45deg_AP",
        notes="Classic SVS, AP rotation"),
    _ds("philips", "philips_dcm", "philips/DICOM/SV_phantom_45deg_RL",
        notes="Classic SVS, RL rotation"),
    _ds("philips", "philips_dcm", "philips/DICOM/SV_phantom_center_no_Water_Suppression",
        bids_suffix="_mrsref",
        notes="Water-reference (no WS) — _mrsref companion"),
    _ds("philips", "philips_dcm", "philips/DICOM_enhanced_multi_dynamic/svsWSAntCing_S002",
        notes="Enhanced multi-dynamic SVS"),
    _ds("philips", "philips_dcm", "philips/DICOM_enhanced_multi_dynamic/press_mega",
        notes="Enhanced multi-dynamic MEGA-PRESS (edit-on/off)"),
    _ds("philips", "philips_dcm", "philips/hyper/converted_dcm.dcm",
        notes="HYPER edit sequence"),

    # ---- UIH (Phase 3) ----
    _ds("uih", "uih", "UIH/mrs_data/dicom/svs_press_te144_SVS_801/00000001.dcm",
        notes="SVS PRESS TE 144ms"),
    _ds("uih", "uih", "UIH/mrs_data/dicom/csi_hise_te144_CSI_1201/00000000.dcm",
        bids_suffix="_mrsi", notes="2D CSI HISE TE 144ms"),
    _ds("uih", "uih", "UIH/mrs_3d/dicom/csi_hise_3d_te144_CSI_1301/00000000.dcm",
        bids_suffix="_mrsi", notes="3D CSI HISE"),
]


# ---------- minimal NIfTI parsing (no nibabel dependency) ----------

class NiftiHeader(NamedTuple):
    sizeof_hdr: int   # 348 (NIfTI-1) or 540 (NIfTI-2)
    dim: tuple        # 8 ints
    pixdim: tuple     # 8 floats
    datatype: int     # 32 = DT_COMPLEX64
    bitpix: int
    vox_offset: int
    sform_code: int
    qform_code: int
    srow_x: tuple     # 4 floats
    srow_y: tuple
    srow_z: tuple


def _open_maybe_gz(p: Path):
    return gzip.open(p, "rb") if p.suffix == ".gz" else open(p, "rb")


def read_nifti_header(p: Path) -> NiftiHeader:
    with _open_maybe_gz(p) as f:
        head = f.read(4)
    sizeof_hdr = struct.unpack("<i", head)[0]
    if sizeof_hdr == 348:
        return _read_nifti1(p)
    if sizeof_hdr == 540:
        return _read_nifti2(p)
    raise ValueError(f"unknown NIfTI sizeof_hdr={sizeof_hdr} for {p}")


def _read_nifti1(p: Path) -> NiftiHeader:
    with _open_maybe_gz(p) as f:
        h = f.read(348)
    dim = struct.unpack("<8h", h[40:56])
    datatype = struct.unpack("<h", h[70:72])[0]
    bitpix = struct.unpack("<h", h[72:74])[0]
    pixdim = struct.unpack("<8f", h[76:108])
    vox_offset = int(struct.unpack("<f", h[108:112])[0])
    qform_code = struct.unpack("<h", h[252:254])[0]
    sform_code = struct.unpack("<h", h[254:256])[0]
    srow_x = struct.unpack("<4f", h[280:296])
    srow_y = struct.unpack("<4f", h[296:312])
    srow_z = struct.unpack("<4f", h[312:328])
    return NiftiHeader(348, dim, pixdim, datatype, bitpix, vox_offset,
                       sform_code, qform_code, srow_x, srow_y, srow_z)


def _read_nifti2(p: Path) -> NiftiHeader:
    with _open_maybe_gz(p) as f:
        h = f.read(540)
    # NIfTI-2 layout
    datatype = struct.unpack("<h", h[12:14])[0]
    bitpix = struct.unpack("<h", h[14:16])[0]
    dim = struct.unpack("<8q", h[16:80])
    pixdim_dbl = struct.unpack("<8d", h[112:176])
    pixdim = tuple(float(v) for v in pixdim_dbl)
    vox_offset = struct.unpack("<q", h[168:176])[0]  # wait — offset in NIfTI-2 is at 168
    # Actually for NIfTI-2: dim @16-80 (8x int64), intent_p1/p2/p3 @80-104,
    # pixdim @104-168 (8x double), vox_offset @168-176 (int64),
    # scl_slope/inter @176-192, cal_max/min @192-208, slice_duration @208-216,
    # toffset @216-224, slice_start/end @224-240, descrip @240-320, aux_file @320-344,
    # qform_code/sform_code @344-352, quatern_b/c/d @352-376, qoffset_x/y/z @376-400,
    # srow_x @400-432, srow_y @432-464, srow_z @464-496
    pixdim = struct.unpack("<8d", h[104:168])
    vox_offset = struct.unpack("<q", h[168:176])[0]
    qform_code = struct.unpack("<i", h[344:348])[0]
    sform_code = struct.unpack("<i", h[348:352])[0]
    srow_x = struct.unpack("<4d", h[400:432])
    srow_y = struct.unpack("<4d", h[432:464])
    srow_z = struct.unpack("<4d", h[464:496])
    return NiftiHeader(540, dim, tuple(float(v) for v in pixdim), datatype,
                       bitpix, vox_offset, sform_code, qform_code,
                       tuple(float(v) for v in srow_x),
                       tuple(float(v) for v in srow_y),
                       tuple(float(v) for v in srow_z))


def read_nifti_payload(p: Path) -> bytes:
    hdr = read_nifti_header(p)
    with _open_maybe_gz(p) as f:
        f.seek(hdr.vox_offset)
        return f.read()


# ---------- conversion drivers ----------

def run_spec2nii(ds: Dataset, outdir: Path) -> tuple[Path, Path | None]:
    """Run spec2nii on a dataset; return (nifti_path, json_sidecar_path)."""
    stem = "spec2nii"
    cmd = ["spec2nii", ds.spec2nii_cmd,
           "-f", stem,
           "-o", str(outdir),
           "-j",
           str(ds.source)]
    try:
        subprocess.run(cmd, check=True, capture_output=True, text=True, timeout=120)
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"spec2nii failed for {ds.id}: {e.stderr or e.stdout}") from e
    # spec2nii writes <stem>.nii.gz and -j writes <stem>.json
    nii = next(iter(sorted(outdir.glob(f"{stem}*.nii.gz"))), None)
    if nii is None:
        raise RuntimeError(f"spec2nii produced no .nii.gz in {outdir}")
    sidecar = nii.with_suffix("").with_suffix(".json")
    return nii, (sidecar if sidecar.exists() else None)


def run_dcm2niix(ds: Dataset, outdir: Path) -> tuple[Path, Path | None]:
    """Run dcm2niix on a dataset; return (nifti_path, json_sidecar_path)."""
    stem = "dcm2niix"
    # dcm2niix -b y emits sidecar, -z n keeps .nii (uncompressed for byte-level
    # comparison; we explicitly decompress spec2nii's .nii.gz when diffing).
    cmd = [str(DCM2NIIX_BIN),
           "-b", "y", "-z", "n",
           "-f", stem,
           "-o", str(outdir),
           str(ds.source)]
    try:
        subprocess.run(cmd, check=True, capture_output=True, text=True, timeout=120)
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"dcm2niix failed for {ds.id}: {e.stderr or e.stdout}") from e
    niftis = sorted(outdir.glob(f"{stem}*.nii"))
    if not niftis:
        raise RuntimeError(f"dcm2niix produced no .nii in {outdir} (cmd: {' '.join(cmd)})")
    nii = niftis[0]
    sidecar = nii.with_suffix(".json")
    return nii, (sidecar if sidecar.exists() else None)


# ---------- comparison ----------

class CompareResult(NamedTuple):
    dataset: Dataset
    fid_match: bool
    sform_match: bool
    dim_match: bool
    sidecar_diff: dict
    fid_byte_diff: int
    notes: list[str]


def compare(ds: Dataset) -> CompareResult:
    notes: list[str] = []
    with tempfile.TemporaryDirectory() as td_spec, tempfile.TemporaryDirectory() as td_dcm:
        td_spec_p = Path(td_spec); td_dcm_p = Path(td_dcm)
        try:
            spec_nii, spec_json = run_spec2nii(ds, td_spec_p)
        except RuntimeError as e:
            return CompareResult(ds, False, False, False, {}, -1, [f"spec2nii ERROR: {e}"])
        try:
            dcm_nii, dcm_json = run_dcm2niix(ds, td_dcm_p)
        except RuntimeError as e:
            return CompareResult(ds, False, False, False, {}, -1, [f"dcm2niix ERROR: {e}"])

        spec_hdr = read_nifti_header(spec_nii)
        dcm_hdr = read_nifti_header(dcm_nii)
        spec_fid = read_nifti_payload(spec_nii)
        dcm_fid = read_nifti_payload(dcm_nii)

        # FID payload compare
        fid_byte_diff = sum(1 for a, b in zip(spec_fid, dcm_fid) if a != b) + abs(len(spec_fid) - len(dcm_fid))
        fid_match = (fid_byte_diff == 0)

        # dim compare — spec2nii NIfTI-2 dim is 8x int64, dcm2niix NIfTI-1 is 8x int16
        dim_match = tuple(spec_hdr.dim) == tuple(dcm_hdr.dim)
        if not dim_match:
            notes.append(f"dim spec={spec_hdr.dim} dcm={dcm_hdr.dim}")

        # sform compare — float32 tolerance (1e-4 absolute, 1e-5 relative)
        sform_match = True
        for row_name, sp_row, dc_row in (("x", spec_hdr.srow_x, dcm_hdr.srow_x),
                                          ("y", spec_hdr.srow_y, dcm_hdr.srow_y),
                                          ("z", spec_hdr.srow_z, dcm_hdr.srow_z)):
            for k, (s, d) in enumerate(zip(sp_row, dc_row)):
                if abs(s - d) > max(1e-4, 1e-5 * abs(s)):
                    sform_match = False
                    notes.append(f"sform[{row_name}][{k}] spec={s} dcm={d} (delta={s-d:.3e})")

        # Sidecar diff
        sidecar_diff: dict = {"ref_only": [], "out_only": [], "differing": []}
        if spec_json and dcm_json:
            spec_meta = json.loads(spec_json.read_text())
            dcm_meta = json.loads(dcm_json.read_text())
            # Alias resolution runs BEFORE the ignore-list filter so we
            # catch the spec2nii-name in spec_meta vs the dcm2niix-name in
            # dcm_meta even when the latter is in our "wide DICOM provenance"
            # bucket (e.g. ReceiveCoilName is informational on the MRS path
            # but matches spec2nii's RxCoil one-for-one).
            raw_spec_keys = set(spec_meta.keys())
            raw_dcm_keys = set(dcm_meta.keys())
            spec_keys = raw_spec_keys - IGNORE_FIELDS_GLOBAL
            dcm_keys = raw_dcm_keys - IGNORE_FIELDS_GLOBAL
            for spec_name, dcm_name in BIDS_MRS_ALIASES.items():
                if spec_name in raw_spec_keys and dcm_name in raw_dcm_keys:
                    spec_keys.discard(spec_name)
                    dcm_keys.discard(dcm_name)
            for k in sorted(spec_keys - dcm_keys):
                sidecar_diff["ref_only"].append((k, spec_meta[k]))
            for k in sorted(dcm_keys - spec_keys):
                sidecar_diff["out_only"].append((k, dcm_meta[k]))
            # Generic float tolerance: spec2nii carries float64 throughout
            # while dcm2niix's C struct routes some MRS fields through float32
            # before re-promotion to double at the JSON sidecar; the last-
            # place noise (1 float64 ULP — 0.068 emits as 0.06799999999999999
            # when parsed from JSON) is not a parity bug. 5 ULPs covers both
            # the JSON-roundtrip noise AND the modest precision loss from the
            # float32 staging step (e.g. SpectrometerFrequency 297.219572 vs
            # 297.219574 from a 7T scanner).
            def _floats_close(a, b, ulps=5, rel=1e-5):
                try:
                    a, b = float(a), float(b)
                except (TypeError, ValueError):
                    return False
                import math
                if a == b:
                    return True
                eps = math.ulp(abs(a)) * ulps
                return abs(a - b) <= max(eps, rel * abs(a))

            for k in sorted(spec_keys & dcm_keys):
                s, d = spec_meta[k], dcm_meta[k]
                if s == d:
                    continue
                # List-of-numbers: accept if every entry is within tolerance.
                if isinstance(s, list) and isinstance(d, list) and len(s) == len(d) \
                        and all(_floats_close(a, b) for a, b in zip(s, d)):
                    continue
                # Single number: tolerance check.
                if _floats_close(s, d):
                    continue
                sidecar_diff["differing"].append((k, s, d))
        elif not spec_json:
            notes.append("spec2nii produced no -j sidecar")
        elif not dcm_json:
            notes.append("dcm2niix produced no -b sidecar")

        return CompareResult(ds, fid_match, sform_match, dim_match,
                             sidecar_diff, fid_byte_diff, notes)


# ---------- CLI ----------

def _trunc(v) -> str:
    """Compact print for sidecar values (long arrays/strings truncated)."""
    if isinstance(v, list) and len(v) > 6:
        head = ", ".join(repr(x) for x in v[:3])
        return f"[{head}, ... +{len(v)-3} more]"
    s = repr(v)
    return s if len(s) <= 120 else (s[:117] + "...")


def print_result(res: CompareResult, verbose: bool = False) -> None:
    ds = res.dataset
    fid_tag = "FID✓" if res.fid_match else f"FID✗({res.fid_byte_diff}B)"
    sform_tag = "sform✓" if res.sform_match else "sform✗"
    dim_tag = "dim✓" if res.dim_match else "dim✗"
    ref_only = res.sidecar_diff.get("ref_only", [])
    differing = res.sidecar_diff.get("differing", [])
    # Parity-critical sidecar surface: spec2nii had it, we don't (we may be
    # missing data) + values that genuinely differ. We do NOT count dcm-only
    # fields as parity failures (per Q4, our wider sidecar is by design).
    n_parity_diff = len(ref_only) + len(differing)
    sidecar_tag = "JSON✓" if n_parity_diff == 0 else f"JSON Δ{n_parity_diff}"
    parity_ok = (res.fid_match and res.sform_match and res.dim_match and n_parity_diff == 0)
    status = "PASS" if parity_ok else "FAIL"
    print(f"[{status}] {ds.id:55s} {fid_tag:11s} {sform_tag:8s} {dim_tag:6s} {sidecar_tag:11s}  ({ds.notes})")
    if not (verbose or status == "FAIL"):
        return
    for note in res.notes:
        print(f"      • {note}")
    for k, v in ref_only:
        alias = f" (alias for dcm2niix's {BIDS_MRS_ALIASES[k]})" if k in BIDS_MRS_ALIASES else ""
        print(f"      spec-only:  {k}: {_trunc(v)}{alias}")
    for k, s, d in differing:
        print(f"      diff {k}: spec={_trunc(s)} dcm={_trunc(d)}")
    # Only show dcm-only fields under --verbose (they're informational per Q4)
    if verbose:
        for k, v in res.sidecar_diff.get("out_only", []):
            print(f"      dcm-only:   {k}: {_trunc(v)}")


def main() -> int:
    ap = argparse.ArgumentParser(description="spec2nii vs dcm2niix DICOM-MRS parity check")
    ap.add_argument("dataset", nargs="?", help="Dataset id (or omit with --all/--list)")
    ap.add_argument("--all", action="store_true", help="Run every dataset")
    ap.add_argument("--list", action="store_true", help="Print the dataset inventory")
    ap.add_argument("--vendor", help="Restrict --all to one vendor (siemens|philips|uih)")
    ap.add_argument("--verbose", "-v", action="store_true", help="Detail every diff (default: only FAILs)")
    args = ap.parse_args()

    if args.list:
        for ds in DATASETS:
            present = "✓" if ds.source.exists() else "MISSING"
            print(f"{present:8} {ds.id:60s} {ds.bids_suffix:10s} {ds.notes}")
        return 0

    targets = list(DATASETS)
    if args.vendor:
        targets = [d for d in targets if d.vendor == args.vendor.lower()]
    if not args.all:
        if not args.dataset:
            ap.error("either pass a dataset id, or use --all / --list")
        targets = [d for d in targets if d.id == args.dataset]
        if not targets:
            ap.error(f"unknown dataset id {args.dataset!r}. --list to see them.")

    if not DCM2NIIX_BIN.exists():
        print(f"dcm2niix binary missing: {DCM2NIIX_BIN}", file=sys.stderr)
        print("Build first: (cd build && make)", file=sys.stderr)
        return 1
    if not shutil.which("spec2nii"):
        print("spec2nii not on PATH; install: pip install spec2nii", file=sys.stderr)
        return 1

    n_pass = n_fail = n_skip = 0
    for ds in targets:
        if not ds.source.exists():
            print(f"[SKIP] {ds.id:60s} source missing: {ds.source}")
            n_skip += 1
            continue
        try:
            res = compare(ds)
        except Exception as e:
            print(f"[FAIL] {ds.id:60s} exception: {e}")
            n_fail += 1
            continue
        # Parity-critical: FID + sform + dim, plus sidecar fields that are
        # either spec-only (we're missing something) or differing-value.
        # dcm-only entries are by-design dcm2niix-richer-sidecar per Q4.
        n_parity_diff = (len(res.sidecar_diff.get("ref_only", []))
                         + len(res.sidecar_diff.get("differing", [])))
        ok = (res.fid_match and res.sform_match and res.dim_match
              and n_parity_diff == 0)
        print_result(res, verbose=args.verbose)
        if ok:
            n_pass += 1
        else:
            n_fail += 1

    print()
    print(f"summary: {n_pass} pass, {n_fail} fail, {n_skip} skipped (total {n_pass+n_fail+n_skip})")
    return 0 if n_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
