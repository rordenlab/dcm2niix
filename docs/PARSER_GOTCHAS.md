# Parser Gotchas Reference

Use this before changing DICOM tag descent, sequence handling, compression/transfer syntax, encapsulation, slice geometry, or vendor-private parsing.

## Sequence And Tag Descent

The sequence latches `sqDepth04000561`, `is00089092SQ`, `sqDepthIcon`, and `is00540016SQ` scope nested tags; first-hit and depth semantics are intentional. `isSQ()` is the implicit-VR sequence allowlist: adding a tag recurses, omitting a tag treats the sequence as opaque. The commented-out `kCodeMeaning` LOCALIZER detector is intentional because referenced-image inheritance false-positives; Siemens XA localizers use private `(0021,103F) kAutoAlignData`.

## Vendor Parsing

UIH MOSAIC bvecs prefer private `(0065,1037)` over standard `(0018,9089)` regardless of file order. Enhanced single-volume slice spacing fallback uses first/last IPP when spacing is absent and Siemens/GE/UIH `InStackPositionNumber` is gated out. The high-slice path must still finalize affine when `dim[3] > kMaxEPI3D`.

Philips Dixon (issue 1038): Water/Fat/In/Out-of-phase all report `ComplexImageComponent=MAGNITUDE`; the type comes from ImageType tokens `_W_`/`_F_`/`_IP_`/`_OP_` (classic `0008,0008` or legacy per-frame `2005,140F`, stable 5.1–12.3; R12 `FrameType` long-form names are not parsed). Matching is ungated because classic `ImageType` precedes `Manufacturer`. `dixonType` must split in `isSameSet`, the `gradDynVol` tie test, and `isScaleOrTEVaries` (TSE Dixon shares one TE). In the `isKludgeIssue809` rewrite, `dimensionIndexValues[k]` for `k >= nDimIndxVal` is the previous frame's rewrite, not file data. R12 leaves the Effective Echo Time index 0 for every echo, so the sort keys on TE; `qsort` is unstable and ties scramble echoes/slices. `isRealIsPhaseMapHz` is sticky per file: the per-frame copy only relabels Real volumes, magnitude keeps the file value so BidsGuess files a B0 map's magnitude under `fmap`. PAR/REC `dti4D` is malloc'd, so `nii_readParRec` must set `dixonType`/`isRealIsPhaseMapHz` per volume (they index tables in `saveDcm2Nii`). Regression: `dcm_qa_dixon`.

## Compression / Encapsulation

`kCompressJP2K` is the JPEG2000 transfer syntax marker, not the runtime decode toggle; `compressFlag` is the runtime decode toggle; RLE is `kCompressRLE`. JPEG lossless C3 with multi-item offset table bypasses the generic `dti4D->offsetTable[]` loop. Multi-fragment single-frame encapsulation uses `reassembleEncapsulatedFragments()` and is admitted only when `numberOfFrames <= 1`.

## Geometry

UIH MRS IOP encodes direction times voxel size; only UIH branches may normalize these non-unit rows. The 3D PhaseEncodingDirection gate intentionally keeps `!SE` to avoid SPACE/FLAIR regressions (issue849) while allowing 3D EPI/GRE (and detected EPI where ETL is unset — see `bandwidthPerPixelPhaseEncode` gate, issue 1024). Derived-image warning suppression is deliberate — one diagnostic, not a cascade: the slice-direction and "missing 0020,0037" orientation warnings stay silent for derived / non-spatial images (e.g. Siemens color-FA).
