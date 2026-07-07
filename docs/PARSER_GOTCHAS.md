# Parser Gotchas Reference

Use this before changing DICOM tag descent, sequence handling, compression/transfer syntax, encapsulation, slice geometry, or vendor-private parsing.

## Sequence And Tag Descent

The sequence latches `sqDepth04000561`, `is00089092SQ`, `sqDepthIcon`, and `is00540016SQ` scope nested tags; first-hit and depth semantics are intentional. `isSQ()` is the implicit-VR sequence allowlist: adding a tag recurses, omitting a tag treats the sequence as opaque. The commented-out `kCodeMeaning` LOCALIZER detector is intentional because referenced-image inheritance false-positives; Siemens XA localizers use private `(0021,103F) kAutoAlignData`.

## Vendor Parsing

UIH MOSAIC bvecs prefer private `(0065,1037)` over standard `(0018,9089)` regardless of file order. Enhanced single-volume slice spacing fallback uses first/last IPP when spacing is absent and Siemens/GE/UIH `InStackPositionNumber` is gated out. The high-slice path must still finalize affine when `dim[3] > kMaxEPI3D`.

## Compression / Encapsulation

`kCompressJP2K` is the JPEG2000 transfer syntax marker, not the runtime decode toggle; `compressFlag` is the runtime decode toggle; RLE is `kCompressRLE`. JPEG lossless C3 with multi-item offset table bypasses the generic `dti4D->offsetTable[]` loop. Multi-fragment single-frame encapsulation uses `reassembleEncapsulatedFragments()` and is admitted only when `numberOfFrames <= 1`.

## Geometry

UIH MRS IOP encodes direction times voxel size; only UIH branches may normalize these non-unit rows. The 3D PhaseEncodingDirection gate intentionally keeps `!SE` to avoid SPACE/FLAIR regressions (issue849) while allowing 3D EPI/GRE (and detected EPI where ETL is unset — see `bandwidthPerPixelPhaseEncode` gate, issue 1024). Derived-image warning suppression is deliberate — one diagnostic, not a cascade: the slice-direction and "missing 0020,0037" orientation warnings stay silent for derived / non-spatial images (e.g. Siemens color-FA).
