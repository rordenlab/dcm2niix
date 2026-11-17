#!/usr/bin/env python3
"""DICOM SEG -> label NIfTI self-check (saveDcm2NiiSEG). Builds tiny synthetic SEGs.

uv run --no-project --with pydicom --with numpy python3 tools/test_seg.py [path/to/dcm2niix]
"""
import json, struct, subprocess, sys, tempfile
from pathlib import Path

import numpy as np
from pydicom.dataset import Dataset, FileMetaDataset
from pydicom.sequence import Sequence
from pydicom.uid import ExplicitVRLittleEndian, ImplicitVRLittleEndian, generate_uid

EXE = sys.argv[1] if len(sys.argv) > 1 else "dcm2niix"


def item(**kw):
    ds = Dataset()
    for k, v in kw.items():
        setattr(ds, k, v)
    return ds


def code(value, meaning):
    return Sequence([item(CodeValue=value, CodingSchemeDesignator="SCT", CodeMeaning=meaning)])


def make_seg(path, zs, segs, orient=(1, 0, 0, 0, 1, 0), spacing=1.0, shared_seg=False, implicit=False, source_seg=0, refs=(), other_study=False):
    """3x3 frames, centre voxel set; frame f at z (along the slice normal) belongs to segs[f]."""
    row, col = np.array(orient[:3], float), np.array(orient[3:], float)
    normal = np.cross(row, col)
    ds = Dataset()
    ds.file_meta = FileMetaDataset()
    ds.file_meta.TransferSyntaxUID = ImplicitVRLittleEndian if implicit else ExplicitVRLittleEndian
    ds.SOPClassUID = ds.file_meta.MediaStorageSOPClassUID = "1.2.840.10008.5.1.4.1.1.66.4"
    ds.SOPInstanceUID = ds.file_meta.MediaStorageSOPInstanceUID = generate_uid()
    ds.StudyInstanceUID, ds.SeriesInstanceUID = generate_uid(), generate_uid()
    ds.Modality, ds.SeriesNumber, ds.SeriesDescription = "SEG", 1, "seg"
    ds.ImageType = ["DERIVED", "PRIMARY"]
    ds.Rows = ds.Columns = 3
    ds.SamplesPerPixel, ds.PhotometricInterpretation = 1, "MONOCHROME2"
    ds.BitsAllocated, ds.BitsStored, ds.HighBit, ds.PixelRepresentation = 1, 1, 0, 0
    ds.NumberOfFrames = len(zs)
    ds.SegmentationType = "BINARY"
    if refs:  # source series; other_study puts them under (0008,1200), as for images in another study
        rs = Sequence([item(SeriesInstanceUID=u, ReferencedInstanceSequence=Sequence([item(
            ReferencedSOPClassUID="1.2.840.10008.5.1.4.1.1.2", ReferencedSOPInstanceUID=generate_uid())])) for u in refs])
        if other_study:
            ds.StudiesContainingOtherReferencedInstancesSequence = Sequence([item(StudyInstanceUID=generate_uid(), ReferencedSeriesSequence=rs)])
        else:
            ds.ReferencedSeriesSequence = rs
    ds.SegmentSequence = Sequence([
        item(SegmentedPropertyCategoryCodeSequence=code("123037004", "Anatomical Structure"), SegmentNumber=s,
             SegmentLabel=f"seg{s}", RecommendedDisplayCIELabValue=[65535, 32896, 32896], SegmentAlgorithmType="MANUAL",
             SegmentedPropertyTypeCodeSequence=code("12738006", "Brain"))
        for s in sorted(set(segs))])
    shared = item(PlaneOrientationSequence=Sequence([item(ImageOrientationPatient=list(orient))]),
                  PixelMeasuresSequence=Sequence([item(PixelSpacing=[1, 1], SliceThickness=spacing, SpacingBetweenSlices=spacing)]))
    if shared_seg:
        shared.SegmentIdentificationSequence = Sequence([item(ReferencedSegmentNumber=segs[0])])
    ds.SharedFunctionalGroupsSequence = Sequence([shared])
    frames = []
    for f, (z, s) in enumerate(zip(zs, segs)):
        fr = item(FrameContentSequence=Sequence([item(DimensionIndexValues=[s, f + 1])]),
                  PlanePositionSequence=Sequence([item(ImagePositionPatient=[round(float(v), 6) for v in z * normal])]))
        if source_seg:  # a source reference may name another segment; it must not be taken as this frame's
            fr.DerivationImageSequence = Sequence([item(DerivationCodeSequence=code("113076", "Segmentation"), SourceImageSequence=Sequence([item(
                ReferencedSOPClassUID=ds.SOPClassUID, ReferencedSOPInstanceUID=generate_uid(), ReferencedSegmentNumber=source_seg,
                PurposeOfReferenceCodeSequence=code("121322", "Source image for image processing operation"))]))])
        if not shared_seg:
            fr.SegmentIdentificationSequence = Sequence([item(ReferencedSegmentNumber=s)])
        frames.append(fr)
    ds.PerFrameFunctionalGroupsSequence = Sequence(frames)
    px = np.zeros((len(zs), 3, 3), np.uint8)
    px[:, 1, 1] = 1
    ds.PixelData = np.packbits(px.ravel(), bitorder="little").tobytes()
    ds.save_as(path, enforce_file_format=True)


def convert(tmp, name, args=(), **kw):
    out = tmp / name
    out.mkdir()
    make_seg(out / "in.dcm", **kw)
    r = subprocess.run([EXE, "-s", "y", "-z", "n", *args, "-f", name, "-o", str(out), str(out / "in.dcm")], capture_output=True, text=True)
    nii = out / f"{name}.nii"
    if not nii.exists():
        return r.returncode, None, None, r.stdout + r.stderr
    h = nii.read_bytes()
    dim = struct.unpack("<8h", h[40:56])[1:4]
    vol = np.frombuffer(h[352:], np.uint8).reshape(dim[::-1])  # [z][y][x]
    return r.returncode, dim, vol, json.loads((out / f"{name}.json").read_text())


def main():
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        rc, dim, vol, js = convert(tmp, "control", zs=[0, 1, 2], segs=[1, 1, 1])
        assert rc == 0 and dim == (3, 3, 3) and vol.sum() == 3, (rc, dim)
        assert js["Segments"][0]["SegmentNumber"] == 1 and js["Segments"][0]["RGB"] == [255, 255, 255], js["Segments"]
        rc, dim, ivol, _ = convert(tmp, "implicit", zs=[0, 1, 2], segs=[1, 1, 1], implicit=True)
        assert rc == 0 and dim == (3, 3, 3) and np.array_equal(ivol, vol), (rc, dim)
        rc, dim, vol, _ = convert(tmp, "shared", zs=[0, 1, 2], segs=[1, 1, 1], shared_seg=True)
        assert rc == 0 and dim == (3, 3, 3) and vol.sum() == 3, (rc, dim)
        for name, spacing in (("sparse", 1.0), ("sparse_bad_declared", 2.0)):  # slice 1 empty
            rc, dim, vol, _ = convert(tmp, name, zs=[0, 2, 3], segs=[1, 1, 1], spacing=spacing)
            assert rc == 0 and dim == (3, 3, 4) and vol[:, 1, 1].tolist() == [1, 0, 1, 1], (name, rc, dim)
        rc, dim, vol, _ = convert(tmp, "labels", zs=[0, 1], segs=[1, 2])
        assert rc == 0 and vol[:, 1, 1].tolist() == [1, 2], (rc, dim)  # flipZ-agnostic: slice 0 is the lowest z
        for name, shared in (("source_ref", False), ("source_ref_shared", True)):
            rc, dim, vol, _ = convert(tmp, name, zs=[0, 1, 2], segs=[1, 1, 1], shared_seg=shared, source_seg=2)
            assert rc == 0 and dim == (3, 3, 3) and sorted(set(vol.ravel())) == [0, 1], (name, rc, dim)
        rc, dim, _, log = convert(tmp, "overlap", zs=[0, 0], segs=[1, 2])
        assert rc != 0 and dim is None and "overlap segment 1 and 2: 1 voxels" in log, (rc, log)
        rc, dim, _, log = convert(tmp, "overlap256", zs=[0] * 256, segs=list(range(1, 257)))  # per-voxel count must not wrap
        assert rc != 0 and dim is None and "segments overlap (1 voxels)" in log, (rc, log)
        rc, dim, vol, _ = convert(tmp, "oblique", zs=list(range(201)), segs=[1] * 201, orient=(1, 0, 0, 0, 0.866025, 0.5))
        assert rc == 0 and dim == (3, 3, 201) and vol.sum() == 201, (rc, dim)
        # Every referenced series, in order, for both reference layouts and both VRs (1.2.3 must not swallow 1.2.34).
        for implicit in (False, True):
            for other in (False, True):
                name = f"refs_{'i' if implicit else 'e'}{'_other' if other else ''}"
                rc, _, _, js = convert(tmp, name, args=("-ba", "o"), zs=[0], segs=[1], implicit=implicit, refs=("1.2.34", "1.2.3"), other_study=other)
                assert rc == 0 and js.get("ReferencedSeriesInstanceUID") == ["1.2.34", "1.2.3"], (name, js.get("ReferencedSeriesInstanceUID"))
    print("test_seg: OK")


if __name__ == "__main__":
    main()
