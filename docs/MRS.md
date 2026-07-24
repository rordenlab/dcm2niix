# MR Spectroscopy Reference

Use this before changing `saveDcm2NiiMRS`, `saveDcm2NiiMRSI`, BIDS-MRS sidecars, NIfTI-MRS ecode 44, or `tools/mrs_post.py`.

## Routing And Parsing

Standard MR Spectroscopy Storage SOP or Siemens CSA Non-Image private FID routes to MRS only after gzip/XML and CMRR physio sniffs fail and MRS corroboration exists; Siemens derived DIFFUSION blobs must stay out of FID routing. `readCSAforMRS()` is MRS-gated; keep the per-tag pre-walk because string/multifloat handlers assume safe bounds. Rx coil CSA short-name overrides the public long name; if both CSA names diverge and are non-empty, enforce spec2nii `ReceivingCoil` precedence.

## SVS / Sidecar

XA negates imaginary samples except `+0.0`; VE/VX does not. Spectral width comes only from `mrsSpectralWidthHz()`, preferring integer-ns `RealDwellTime` over the CSA float. MRS sidecar precision/order is load-bearing: `SpectralWidth %.17g`, frequency `%.9g`, voxel size `[xyzMM[2], xyzMM[1], zThick]`, `InversionTime` emits 0. F1 pixdim swap keeps affine, `pixdim`, and `AcquisitionVoxelSize` mirroring spec2nii row1/row2 ordering (grep `F1 pixdim-mirror`). UIH SVS/MRSI IOP rows encode direction times voxel size; normalize only under UIH gates and keep UIH half-shifts. Philips Enhanced SlabOrientation uses FD `dcmMultiFloatDouble`, captures up to two rows, increments count only inside the length guard, and overrides IOP for Philips MRS (known gap: `geomValid` still tests IOP).

SVS suffix needs explicit SINGLE_VOXEL or no-type plus CSA VOI evidence; `_mrsref` uses naming tokens and flips `isMrsRef`; otherwise leave Unknown. Non-spatial FID warning/suppression is gated by no type, no grid, invalid geometry, and no voxel size; `hasVoiCenter` is not localization evidence. `WaterSuppressed` is derived only from `!d.isMrsRef`; do not add a second source. Philips exact 2x payload writes an `_mrsref` companion; 3x+ multipliers are intentionally dropped, and an oversized classic SVS reads the first expected payload and warns (the exact 2x trailing chunk is the water ref). Classic Siemens CSI/MRSI with a spatial grid must not be admitted as `_svs` without VOI evidence. MRS stack members must agree on MRSness, points, spectral width, endian, manufacturer, XA, and geometry; foreign formats are rejected.

## TDTI / Reporting

Fresh MRS locals must call `initTDTI4D()` so PET/BIDS sentinel arrays do not emit zero-filled spectral axes; `saveDcm2NiiCore` must not call it because enhanced/PAR per-frame data is live. `mrsReportConvert()` exists because MRS/MRSI bypass `saveDcm2NiiCore`; keep the FS-wrapper path suppression and 5th-axis (dynamics) reporting.

## NIfTI-MRS Extension

The ecode 44 extension goes through the shared `nii_saveNII()` hdrExt path; non-MRS output must remain byte-identical, and R in-memory output intentionally lacks the extension. `mrsHdrExtJson()` returns NULL rather than fabricating missing required frequency/nucleus, strips patient and `OriginalFile` under both `-ba y` and `-ba o`, and omits volatile `ConversionTime`. Embedded extension intentionally emits `dim_5: DIM_DYN` for SVS and MRSI, unlike strict BIDS sidecar MRSI suppression. Extension `TxCoil` uses `transmitCoilName`, not `coilName`; `SequenceName` falls back to pulse/CSA only on the MRS gate; `VOI` uses the shared matrix helper.

## MRSI / Post

MRSI dispatch handles Enhanced Row/Plane/Volume and classic Siemens grids: Siemens Enhanced negates imag; classic does not and half-shifts. UIH MRSI normalizes IOP, uses qfac -1, z half-shift, negated cross-product, and rows-fastest transpose (`dim[1]=rows`, `dim[2]=cols`). Siemens non-square CSI pixdim/m_ij swap mirrors spec2nii; UIH/Philips keep raw ordering. MRSI VOI uses double-precision DS/FD reads and `%.17g`, with LPS->RAS flips of rows 0/1 including the slice-normal column.

dcm2niix bundles raw multi-DICOM/multiframe MRS; vendor-state splits/reshapes (CMRR DKD, Philips MEGA-PRESS, HYPER) belong in `tools/mrs_post.py <bundled.nii> --dicoms <dicom-dir-or-file>`, and output suffixes stay BIDS canonical. Siemens sLASER TE summing stays C-side; the DKD reference split stays in `mrs_post.py`. Do not reintroduce the Philips MEGA-PRESS per-frame scanner with `p < sz - 8`/`buf[p+9]` — that bound is OOB.
