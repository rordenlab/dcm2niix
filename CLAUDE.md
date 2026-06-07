# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Prime Directive: Do No Harm

dcm2niix handles real-world DICOM data from dozens of vendors, many of which have idiosyncratic or outright incorrect implementations of the DICOM standard. The codebase is full of intentional kludges, special cases, and workarounds that exist because specific scanners produce data that requires them. Before modifying any code:

- **Assume every quirk is load-bearing.** Vendor-specific branches, magic constants, commented explanations, and compiler directives are clues to hard-won workarounds. Do not simplify, refactor, or remove them without understanding what real-world data they handle.
- **Make surgical, minimal changes.** Touch only what is necessary. Avoid refactoring surrounding code, reorganizing conditionals, or "cleaning up" logic adjacent to your change.
- **Preserve compatibility with malformed data.** Correctness here means handling what scanners actually produce, not what the DICOM spec says they should produce.
- **When in doubt, don't change it.** If you cannot determine why a piece of code exists, leave it alone.

## Project Overview

dcm2niix converts DICOM medical images to NIfTI format. Written in C/C++ with no required external dependencies. Supports 20+ scanner vendors (Siemens, GE, Philips, Canon, UIH, etc.) and multiple compressed transfer syntaxes (JPEG, JPEG-LS, JPEG2000).

## Build

CMake (full features): `mkdir build && cd build && cmake -DZLIB_IMPLEMENTATION=Cloudflare -DUSE_JPEGLS=ON -DUSE_OPENJPEG=ON .. && make` → `build/bin/dcm2niix`. Makefile: `cd console && make` (targets: `debug`, `sanitize`, `jp2`, `turbo`, `wasm`).

CMake options: `-DZLIB_IMPLEMENTATION=Miniz|System|Cloudflare`, `-DUSE_OPENJPEG=OFF|GitHub|System`, `-DUSE_JPEGLS=ON|OFF`, `-DUSE_TURBOJPEG=ON|OFF`, `-DUSE_ZSTD=ON|OFF`, `-DBATCH_VERSION=ON`.

## Testing

**In-tree regression** (required before non-trivial commits): three submodules `dcm_qa`, `dcm_qa_nih`, `dcm_qa_uih`. Run via `/regressiontest` (uses `build/bin/dcm2niix`), or manually:
```bash
git submodule update --init
export PATH="$PWD/build/bin:$PATH"
for d in dcm_qa dcm_qa_nih dcm_qa_uih; do (cd "$d" && ./batch.sh) || { echo "FAIL: $d"; exit 1; }; done
```
The diffs ignore `ConversionSoftwareVersion` and `BidsGuess`. **Full vendor matrix** for releases lives in [`dcm_validate`](https://github.com/neurolabusc/dcm_validate) (~35 submodules). **Pre-push**: run `dcm_qa` minimum, plus `git ls-files | xargs codespell` (matches the `Codespell` GH Action; config in `.codespellrc`).

**MR Spectroscopy regression** lives in the sibling [`dcm_qa_mrs`](https://github.com/neurolabusc/dcm_qa_mrs) repo (peer checkout, not a submodule). Three scripts: `python3 batch.py --corpus={local,spec2nii,both}` for `.json` sidecar regression vs `Ref/<corpus>/`, `python3 compare_spec2nii.py --corpus={local,spec2nii,both}` for live FID/sform/dim/pixdim parity vs spec2nii, and `spec2nii_compare.py` carrying the curated 40-dataset inventory (moved from `dcm2niix/tools/` once the MRS port stabilised — single source of truth). Only `$SPEC2NII_DATA` is needed (points at a clone of `git.fmrib.ox.ac.uk/wclarke/spec2nii_test_data`; nothing auto-downloads).

**Session scratch** (`temp/`): gitignored drop zone for `/audit` agents and ad-hoc DICOM analysis. Holds real PHI; clean (`rm -rf temp/`) at the end of every cycle that wrote to it.

## Architecture

All source in `console/`. Key files:

- **main_console.cpp** — CLI parsing, orchestrates conversion
- **nii_dicom.cpp** (~8.8k lines) — DICOM parser, populates `TDICOMdata`, handles every vendor-specific tag
- **nii_dicom.h** — `TDICOMdata` (280+ fields), `TDTI4D`, `TCSAdata`
- **nii_dicom_batch.cpp** — series grouping, 3D/4D assembly, NIfTI write, BIDS sidecar
- **nifti1_io_core.cpp** — NIfTI writer + JSON sidecar + reorientation
- **nii_foreign.cpp** — PAR/REC (Philips non-DICOM)
- **nii_ortho.cpp** — reorientation, cropping, resampling
- **jpg_0XC3.cpp** — lossless JPEG decoder
- **dicom_fragments.{h,cpp}** — multi-fragment single-frame encapsulation helper (issue #1017)
- **reproin.cpp** — ReproIn one-pass `-f %H` (long-form in [REPROIN.md](REPROIN.md))

Bundled libs: miniz, cJSON, NanoJPEG, CharLS (`console/charls/`).

**Data flow:** `main_console.cpp` → `nii_dicom_batch.cpp` (scan dir, group series) → `nii_dicom.cpp` (parse each DICOM → `TDICOMdata`) → `nii_dicom_batch.cpp` (assemble volume) → `nifti1_io_core.cpp` (write).

## Source-list fanout warning

dcm2niix has FIVE source-list surfaces that must agree when adding a `.cpp`: `console/CMakeLists.txt` (3 blocks), `console/makefile`, `console/windows.bat`, `console/notarize.sh`, `COMPILE.md`. An external audit caught the omission in three of these for `dicom_fragments.cpp` (round 7 H1).

## Load-bearing constraints

### TDICOMdata size + by-value passing

`TDICOMdata` is passed BY VALUE through five functions in the save chain (`saveDcm2NiiCore → nii_loadImgXL → headerDcm2Nii → headerDcm2Nii2 → headerDcm2NiiSForm`); `readDICOMx` itself allocates ~1.5 MB of stack. At ~9.6 KB per struct the chain just fits the macOS 8 MB main-thread stack; growing the struct by even ~4 KB tips `headerDcm2NiiSForm`'s prologue probe past the guard page and crashes (issue #877).

**For a new per-file payload, use a heap pointer with lazy `calloc` and a `free_TDICOMdata_<name>` helper called before every `free(dcmList)` site.** See `deID_CS` (issue #877) for the worked pattern: 8-byte pointer in the struct, NULL by default, allocated on first hit, freed via idempotent helper.

Inline POD additions are only acceptable when they are at the noise level of the #877 margin. The 2026-06-07 round-4 audit cleared `slabOrient[7]` (float) + `slabOrientCount` (int) = +32 B (~0.8% of the +4 KB failure margin). Anything ≳ 1 KB MUST use the `deID_CS` heap-pointer template; do not bypass the rule for a "small" array if the inline cost would compound across a chain of future additions.

### `TDICOMdata.deID_CS` ownership contract

`deID_CS` is a heap pointer owned by the `dcmList[]` entry that called `readDICOMx`. Every other holder (by-value params, retained shallow copies in `MRIFSSTRUCT.tdicomData`, R-side `TDicomSeries.representativeData`) gets a **non-owning** shallow copy that must NOT free it.

- After `MRIFSSTRUCT.tdicomData = dcmList[i]` or `series.representativeData = dcmList[i]`, the retained struct MUST set `.deID_CS = NULL; .deID_CS_n = 0;` (sites: `nii_dicom_batch.cpp:~10325, ~11459, ~12485, ~12506`).
- Direct `readDICOM()` callers (not in a cleaned dcmList) MUST `free_TDICOMdata_deID_CS(&d)` before the result goes out of scope.
- `free_TDICOMdata_deID_CS` is idempotent (NULL-safe).

### macOS 16 MB stack

Both `console/makefile` and `console/CMakeLists.txt` set `-Wl,-stack_size -Wl,0x1000000` on macOS (issue #867). LTO inlining + the large `readDICOMx` frame can overflow the default 8 MB ceiling on real-world XA80 multi-study datasets. The cmake gate is `if (APPLE AND (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"))` — without the `AppleClang` clause the block silently skips on stock Xcode.

## Parser gotchas (file:line citations)

These are the kind of "looks weird but is load-bearing" branches the Prime Directive guards. Read the surrounding comment before touching.

- **Sequence-filter latches** in `nii_dicom.cpp`: `sqDepth04000561` (OriginalAttributesSequence, issue #989), `is00089092SQ` (ReferencedImageEvidenceSequence), `sqDepthIcon` (IconImageSequence), `is00540016SQ` (RadiopharmaceuticalInformationSequence — scopes nested CodeMeaning lookups; first hit wins).
- **`kCodeMeaning` LOCALIZER detector** at `nii_dicom.cpp:~5981` is intentionally COMMENTED OUT (false-positives via referenced-image inheritance). XA localizers use the private `(0021,103F) kAutoAlignData` instead (substring `LOCALIZER`, case-insensitive, Siemens-only).
- **UIH MOSAIC bvec**: when `manufacturer == UIH`, prefer private `(0065,1037)` over standard `(0018,9089)` regardless of file order (issue #993).
- **`isSQ()` at `nii_dicom.cpp:~4043`** is the explicit allowlist for implicit-VR SQ descent. Adding a tag here makes the parser recurse; omitting it makes the SQ an opaque blob. PET tags `(0054,0016)`, `(0054,0300)`, `(0054,0304)` were added for issue #983.
- **`kMaxEPI3D` (in nii_dicom.h)** caps `CSA.sliceTiming[]`. Volumes with `dim[3] > kMaxEPI3D` MUST still get affine finalisation — do not early-return out of `headerDcm2Nii2()` in the high-slice branch (commit `1cd1620` fixed a regression there).
- **JPEG Lossless multi-fragment gate** in `nii_loadImgXL()`: when `compressionScheme == kCompressC3` and offset-table has >1 item, short-circuit to `nii_loadImgXLCore()`. The generic `dti4D->offsetTable[]` loop is not safe for C3 (issue #1013). Do not "clean up" this gate.
- **Multi-fragment single-frame encapsulation** (issue #1017): `dicom_fragments.{h,cpp}` exposes `reassembleEncapsulatedFragments()`; per-codec wrappers in `nii_loadImgJPEGC3` (uses `decode_JPEG_SOF_0XC3_mem`) and `nii_loadImgCoreOpenJPEG`. The parser gate at `nii_dicom.cpp:~8190` admits multi-fragment ONLY when `numberOfFrames <= 1`.
- **UIH MRS IOP row normalization** (F2) at `nii_dicom_batch.cpp:~11770`: UIH MRS encodes `(0020,0037) ImageOrientationPatient` as `direction * PixelSpacing` (rows have non-unit magnitude), unlike Siemens/Philips. `saveDcm2NiiMRS` normalizes both row vectors before the m_ij scalings or the sform double-counts voxel size, and applies spec2nii's `half_shift=True` translation `[-0.5, -0.5, 0]` on the first two axes. Gated on `d0->manufacturer == kMANUFACTURER_UIH` so a malformed non-unit IOP from any other vendor stays gated out by the strict-unit `orientShapeOK` check (which also branches on UIH; audit 2026-06-07 H1).
- **Transfer-syntax naming**: `kCompressJP2K` = JPEG2000 transfer syntax tag (was the confusingly-overloaded `kCompressYes`); `compressFlag` is the runtime decode-enabled toggle. RLE is `kCompressRLE`. Audit round 7 caught a global-rename mistake here; double-check `nii_dicom.cpp:~5615-5640` when renaming a compression constant.
- **`-f %v` vs `-f %m`**: full vendor name vs 2-char abbreviation. Case-sensitive. `-f %h` = legacy hazardous BIDS hierarchical; `-f %H` = ReproIn one-pass ([REPROIN.md](REPROIN.md)).

## Single-volume Enhanced DICOM slice-spacing fallback

`readDICOMx` at `nii_dicom.cpp:~8390`: when `(0018,0088) SpacingBetweenSlices` is absent AND the per-frame `InStackPositionNumber` path is gated out for Siemens / GE / UIH (the `case kInStackPositionNumber` vendor exclusion at `nii_dicom.cpp:~6596` keeps `maxInStackPositionNumber=0` to avoid mis-splitting their 4D Enhanced packings), `SliceThickness` alone can mis-report the through-plane sampling — notably Siemens SWI mIP where `(0018,0050)=20mm` is the projection slab depth but adjacent frames are 2.5mm apart. The fallback uses the already-populated `patientPosition[]` (first frame) and `patientPositionLast[]` (last frame) to compute `dx = |last - first| / (xyzDim[3] - 1)`. Gated on `xyzDim[4] < 2` (single volume only — first-to-last IPP doesn't lie along the slice axis in 4D Enhanced packings).

## PhaseEncodingDirection 3D multi-echo gate

`nii_dicom_batch.cpp:~3283`:
```c
if ((d.echoTrainLength > 1) && (strstr(d.scanningSequence, "SE") == NULL))
    isSkipPhaseEncodingAxis = false;
```
The `!SE` clause is load-bearing — without it, 3D SPACE FLAIR (`dcm_qa_xb10/flair`, ETL=214, `ScanningSequence "SE\IR"`) emits PhaseEncodingDirection and the issue849 regression returns. Admits 3D EPI BOLD (`EP`), Philips/GE multi-echo GRE QSM (`GR`, ETL>1). MP-RAGE stays suppressed via underlying ETL=1.

## Siemens BIDS classification

- **`MRWeightingGuess()`** in `nii_dicom_batch.cpp` is the single source of truth for MR weighting classification, shared by Siemens `fl3d_vibe` / `tse2d`, Philips `SK+SE`, GE `FSE`. Source-of-truth order: DICOM `(0008,9209) AcquisitionContrast` (T1/T2/PROTON_DENSITY/T2_STAR/FLUID_ATTENUATED/STIR) → Bottomley + Ernst-angle physics fallback (T1 ≈ `0.8 * B0^0.38`; T2* ≈ `0.050 / B0`; SE T2 threshold = 45 ms TE; GRE thresholds match the historical fl3d_vibe classifier). `isVariableFlipAngle=true` forces `Unknown` on the physics arm.
- **`setBidsFromAcquisitionContrast()`** runs at the very end of `setBids()`, gated on empty `bidsDataType`. Maps `AC=T1/T2/PROTON_DENSITY/T2_STAR/FLUID_ATTENUATED/DIFFUSION/TOF` to BIDS, strictly additive. **`AC=PERFUSION` returns early** (lands in `Unknown/`) — PERFUSION covers ASL AND DSC/DCE, so it can't be a safe fallback target. Modality is appended via bounded `snprintf(suffix + used, kDICOMStrLarge - used, "_%s", modality)`. `AC=DIFFUSION` fallback also requires `d->CSA.numDti >= 1` so a bare AC tag doesn't produce `_dwi` with empty `.bval/.bvec`. **AC=DIFFUSION's `setDiffusion` side effect** (`d.isDiffusion = true`) preserves back-compat for non-AC-aware downstream code.
- **Vendor ASL branches** at `nii_dicom_batch.cpp:~8395` (Siemens), `~8675` (Philips), `~8908` (GE) **do not include AC=PERFUSION as a standalone OR-term** — audit 2026-06-07 dropped that pattern because a DSC/DCE series carrying bare AC=PERFUSION would have been misclassified as ASL. The vendor-positive signals (Siemens sequence-name tokens; Philips `ImageType=PERFUSION` + private `aslFlags`; GE `seqName=asl`) are the sole ASL signals.
- **SPACE / FLAIR detection at `nii_dicom_batch.cpp:~7982`**: the `tse_vfl` branch also fires on `\space` and consults `pulseSequenceName` (XA-line carries the inversion-recovery signal in `pulseSequenceName` like `*spcir_220ns` when `sequenceName` is empty). The bold-cascade PACE check is anchored on `_pace` (not bare `pace` — would substring-match `space`).
- **VIBE / SWI / MPRAGE marketing-label nuances** in `setBidsSiemens` are intentional Siemens-only special cases; see `setBidsSiemens` source for the cascade order. SWI ImageType override fires on `_SWI` in `d->imageType` (underscore-prefix avoids substring drift on `SWIRL`).
- **BidsGuess derivative override**: `nii_dicom_batch.cpp:~1402-1406` matches word-boundary `_FA`/`_ADC`/`_colFA`/`_expADC`/`_trace`/`_S0map` and flips `dataTypeBIDS` to `"dwi"`. Tensor goes to `"derived"` (no canonical raw suffix).

## Sidecar emission gotchas

- **`AcquisitionTime` / `AcquisitionDateTime`** use `%09.6f` (zero-padded), NOT `%02.6f` (BIDS validator's `ACQTIME_FMT` rule).
- **`SequenceName` fallback for XA60 fMRI**: when `d.sequenceName` is empty but `d.pulseSequenceName` is populated, promote the latter into `SequenceName` so the BIDS-recommended field isn't empty.
- **`MatrixCoilMode`** always emits on the Siemens CSA path. `patMode=1`→`"SENSE"`, `=2`→`"GRAPPA"`, else (`32` pure-SMS, `256` CompressedSense, `-1` not-found)→`"None"`. Pure-SMS XA60 acquisitions used to omit the field entirely and trip the validator. Do not "tidy" the `(patMode != 1) && (patMode != 2)` arm or drop the outer `else` (both load-bearing).
- **`PulseSequenceType` heuristic** maps `(ScanningSequence, SequenceVariant, multiBandFactor)` to BIDS-recommended labels (`Multiband Spin Echo EPI`, `Spin Echo EPI`, `MPRAGE`, `Spoiled Gradient Echo`, etc.). `isSP` checks all three legal token positions (`"SP\\"`, sole `"SP"`, mid-or-trailing `"\\SP"`) to avoid `OSP` false-positives.
- **`ParallelReductionFactorInPlane` / `OutOfPlane`** emit at `>= 1.0` (BIDS validator wants the 1.0 case as informative), default sentinel `0.0`.
- **`PartialFourier`** emit gated on `pf < 1.0` (never report full Fourier — `feedback_partial_fourier`).

## ReproIn one-pass (`-f H` / `%H`)

`reproin.cpp` parses `ProtocolName` (fallback `SeriesDescription`) into a BIDS stem, using `StudyDescription` (fallback `PerformedProcedureStepDescription`) as the path prefix. Subject derives from `PatientID` via `reproinFixupSubjectId`; session from protocol or `reproinResolveSession`. Companion flags: `-bi` overrides subject, `-bv` overrides session, `-br` overrides project subdir.

Cross-series concerns (fmap pairing via ShimSetting + affine, `_scans.tsv`, `B0FieldIdentifier`/`Source`, session backfill, zero-padded `__dup-NN`, BIDS scaffolding, participants.tsv) live in `tools/reproinx.py` (stdlib-only). Flags: `--anonymize` (upgrades inner dcm2niix from `-ba o` to `-ba y`), `--strict`, `--keep-derivatives`, `--no-convert`.

**`-ba` modes**: `y` (default; strip dates + PII), `n` (keep both), `o` (omit PII, keep timestamps). `reproinx.py` uses `o` internally so its `_scans.tsv` aggregation has timestamps. `isOmitPiiBIDS` / `isAnonymizeBIDS` are independent.

**Provenance TSV** `<studyRoot>/.reproin_provenance.tsv` is written by `reproinAppendProvenance` once per series, ONLY when filename format contains literal `%H`. Schema is gated on `-ba`: default writes 11 columns (StudyInstanceUID, SeriesNumber, ProtocolName, SeriesDescription, StudyDescription, OutputStem, PatientAge, PatientSex, StudyDate, StudyTime, PatientID); `-ba y` writes 6 columns and drops PatientAge/Sex/Date/Time/ID. The TSV is the privacy boundary between C and Python sides; the writer rotates stale-header files to `.bak` to avoid silently mixing schemas.

**Unknown-rescue + collapse passes** (`_rescue_unknown_dir`, `_maybe_collapse_nonreproin_root`, `_purge_all_discard_unknown`) and the **physio rescue** (`_rescue_physio_recordings`) all run in `reproinx.py:_post_process`. The trust boundary is `out_root` is single-user trusted output; path-traversal hardening on `BidsGuess` entries (`_BIDS_DATATYPES` allowlist + `[A-Za-z0-9_-]*` regex) is the consumption-side privacy/safety gate against malicious `StudyDate` etc.

**`mrs` is in `_BIDS_DATATYPES`**, matching the C side's `BidsGuess ["mrs","_svs"]` (added in stable BIDS 1.11.1+). `ct` is intentionally NOT in the allowlist (BEP-024 not stable); CT files stay in `Unknown/` and the `.bidsignore` sweep routes them.

## MR Spectroscopy (`saveDcm2NiiMRS`)

Pipeline at `nii_dicom_batch.cpp:~11324`. SOP detection: standard MR Spectroscopy Storage UID `1.2.840.10008.5.1.4.1.1.4.2` sets `d.isMRS = true` (XA-line). FID payload offset captured at `(5600,0020)` (NumarisX / XA) or at private `(7FE1,1010)` (Numaris4 / VB/VE under SOP `1.3.12.2.1107.5.9.1` Siemens CSA Non-Image Storage). The private-tag fallback fires only when gzip-XML and CMRR PMU sniffs both fail, the payload is at least 16 bytes / multiple-of-8, AND at least one CSA-derived MRS corroboration signal is present (`dataPointColumns > 0` || `resonantNucleus` set || `spectralWidth > 0` || `dwellTime > 0` || VOI tags populated — audit 2026-06-07 H4). Without corroboration the payload is left as raw / physio.

**CSA-derived spectroscopy metadata** for VB/VE is extracted via `readCSAforMRS()` at `nii_dicom.cpp:~1610`, gated on `isRawDataStorage || isMRS || mrsAcqType != kMRSAcqNone` so non-MRS Siemens CSA files do not trigger it. Reads `ImageOrientationPatient`, `VoiPosition`, `VoiPhase/Readout/Thickness`, `RealDwellTime`, `ImagingFrequency`, `ImagedNucleus`, `SpectroscopyAcquisitionDataColumns`, `RepetitionTime`, `EchoTime`, `InversionTime`, `FlipAngle`, `NumberOfAverages`, `TransmittingCoil`, `ReceivingCoil`. Sentinel-only writes (the destination's init-value gate) so public-tag values keep precedence. The outer tag-loop + `nitems` cap (≤128) are bounds-checked, and a per-tag pre-walk (`nii_dicom.cpp:~1669`) confirms every item header+payload fits in `lLength` BEFORE any handler reads `&buff[lPos]` — `csaMultiFloat` and the string handlers do NOT validate internally, so the pre-walk is load-bearing (audit 2026-06-07 H2 follow-up). Do not remove the pre-walk while keeping the string-handler `memcpy(&itemCSA, &buff[lPos], ...)` reads.

**M4 RxCoil precedence (MRS path)** at `nii_dicom.cpp:~1862`: for MRS-classified files, the CSA `ReceivingCoil` / `ImaCoilString` short-name (spec2nii's `RxCoil` source) overrides the public-tag `(0018,1250) ReceiveCoilName` long marketing label written into `d->coilName`. Tag-name `||` accepts either CSA item and both target the same `d->coilName` slot. **Actual semantics**: when both tags are nonempty, the LATER one in CSA tag order wins (no empty-target guard). spec2nii's stated preference is `ReceivingCoil[0]` over `ImaCoilString[0]` (`dicomfunctions.py:692-697`); the C side does not currently enforce that ordering. Corpus passes because no observed file has both tags populated AND disagreeing — VB/VE only have `ImaCoilString` populated; XA-line agrees across both tags. If a divergent both-nonempty case appears, lock spec2nii precedence by adding `if (d->coilName[0] != '\0') break;` at the top of the `||` branch (source comment carries the explicit pointer to this fix). Non-MRS Siemens scans keep the public-tag long name because the outer `readCSAforMRS()` gate keeps this off the standard image pipeline.

**Phase convention**: NumarisX (XA) negates each odd-indexed (imag) float except `+0.0` (vs spec2nii reference). VE/VX (Numaris4) skips the negation.

**Sidecar required (BIDS-MRS)**: `ResonantNucleus` (array), `SpectrometerFrequency` (array, `%.9g` precision), `SpectralWidth` (`%.17g` precision), `EchoTime`, `WaterSuppressed` (`!d.isMrsRef`). Recommended: `NumberOfSpectralPoints`, `AcquisitionVoxelSize` (ordered `[xyzMM[2], xyzMM[1], zThick]` to mirror the F1 pixdim swap — see "F1 pixdim-mirror" anchor; uses `zThick` not `xyzMM[3]`), `NumberOfTransients` (`NumberOfAverages * dim[5]`), `ScanningSequence` constrained to `SVS`/`MRSI`/`Unlocalized MRS`, `DwellTime = 1/SpectralWidth`, `dim_5: DIM_DYN` (always), `TransmitCoilName`, `InversionTime` (emits 0.0 even when no inversion).

**F1 pixdim swap**: `saveDcm2NiiMRS` builds the sform with spec2nii's row1/row2 convention (`m00 = rx0 * py` where `py = xyzMM[2]`), so the NIfTI x-axis carries VoiReadoutFoV and the y-axis carries VoiPhaseFoV. `pixdim[1]/pixdim[2]` mirror that ordering or the header per-axis voxel sizes contradict the affine. The matching `AcquisitionVoxelSize` emission in `nii_SaveBIDSX` reorders for the same reason. Both sites tagged with the `F1 pixdim-mirror` comment so `grep "F1 pixdim-mirror" console/nii_dicom_batch.cpp` locates both.

**F2 UIH SVS sform (P3.a)**: UIH MRS encodes `(0020,0037) ImageOrientationPatient` as `direction * VoxelSize` (rows have non-unit magnitude == `PixelSpacing`), unlike Siemens/Philips standard unit-IOP. `saveDcm2NiiMRS` at `~11770` normalizes both row vectors before the m_ij scalings (so xyzMM[]/zThick isn't double-counted) and applies spec2nii's `half_shift=True` translation `[-0.5, -0.5, 0]` on the first two NIfTI axes. Both the normalization and the half-shift are gated on `d0->manufacturer == kMANUFACTURER_UIH`; the `orientShapeOK` validity gate also branches on UIH and falls back to the strict `r1mag ∈ [0.5, 1.5]` check for every other vendor so a malformed non-UIH IOP fails closed (sform_code=0 + warn) rather than being silently renormalized (audit 2026-06-07 H1).

**P2.b SlabOrientation Philips Enhanced (45deg_AP fix)**: `TDICOMdata.slabOrient[7]` + `slabOrientCount` (`nii_dicom.h:~287`; `float[7]` + `int` = +32 B; audited 2026-06-07 round-4 K3 as safe against the issue #877 stack-ceiling — see "TDICOMdata size + by-value passing" block). Capture up to two `(0018,9105) SlabOrientation` items from `(0018,9126) VolumeLocalizationSequence`. Parsed at `nii_dicom.cpp:~8464` via `dcmMultiFloatDouble` (VR FD, 8-byte binary; do NOT use `dcmMultiFloat` which assumes DS text). Consumer: `saveDcm2NiiMRS` at `nii_dicom_batch.cpp:~11975` prefers `d0->slabOrient[]` over per-frame `(0020,0037) IOP` for Philips MRS when `slabOrientCount >= 2` and manufacturer is Philips. Rationale: per-frame IOP on Enhanced Philips MRS sometimes mirrors `slabs[1]+slabs[2]` instead of `slabs[0]+slabs[1]` (e.g. `SV_phantom_45deg_AP`), giving the wrong sform; SlabOrientation is the canonical source. `slabOrientCount++` lives INSIDE the `lLength >= 24 && slabOrientCount < 2` guard (audit 2026-06-07 round-4 K4(b) fix) so a zero-length first item can never leave row 1 all-zero while the count reaches the consumer's `>= 2` threshold. **Known gap (audit 2026-06-07 H3, still open / deferred to Phase 6)**: the geometry-validity gate (`geomValid`) is computed from `d0->orient[]`, but the affine row substitution uses `d0->slabOrient[]` — `geomValid=true` for IOP does not guarantee the SlabOrientation rows are well-formed. Theoretical for real Philips Enhanced data (both IOP and SlabOrientation come from the same Phoenix slab geometry); no corpus driver.

**`mrsSpectralWidthHz()`** at `nii_dicom_batch.cpp:~1270` is the single source-of-truth for MRS spectral width, used by both the sidecar (`SpectralWidth`/`DwellTime` emission at `~3180`) and the NIfTI writer (`hdr.pixdim[4]` in `saveDcm2NiiMRS` at `~11618`). Prefers integer-ns Siemens private `(0021,1142)` `RealDwellTime` (`1.0e9 / d.dwellTime`) over CSA-float `d.spectralWidth` — float32 CSA loses ~6 sig figs at typical SVS widths (audit 2026-06-07 H2). Keep the helper canonical: do not re-inline the `dwellTime > 0` gate or compute spectral width directly at either call site.

**SVS / `_mrsref` writer**: `saveDcm2NiiMRS` hard-rejects `mrsAcqType` ROW/PLANE/VOLUME (`nii_dicom_batch.cpp:~11403`). The BIDS-MRS entity suffix is one of:

- `_svs` when `mrsAcqType == kMRSAcqSingleVoxel` OR (`mrsAcqType == kMRSAcqNone` AND CSA VOI tags populated — `zThick > 0` AND `xyzMM[1] > 1.0f` AND `xyzMM[2] > 1.0f`).
- `_mrsref` when the series-naming heuristic (`mrsIsStandaloneWaterRef()` — `wrsoff` / `wrs_off` / `no_Water_Suppression` / `noWS` / `_mrsref` substring in `seriesDescription` / `protocolName` / `sequenceName` / `pulseSequenceName`) marks the acquisition as a standalone water reference; flips `d.isMrsRef = true` so the sidecar emits `WaterSuppressed: false`. The heuristic is in a helper so Phase 6 MRSI / `_mrsiref` can reuse the same token set.
- empty (file lands in `Unknown/`) when neither SVS evidence nor water-ref naming applies — guards against classic Siemens CSI/MRSI inputs (audit 2026-06-07 H3).

**`_mrsref` companion (Philips P2.c)**: when every DICOM in the stack carries exactly 2× expected payload bytes (`bytes_per_dicom`), a parallel `fidRef` buffer captures the trailing water-reference FID alongside the main FID. After the main `_svs` NIfTI/sidecar are written, the companion writer derives `<stem>_mrsref.nii(.gz)` by substituting `_svs → _mrsref` in `pathoutname` (or appending `_mrsref` when no `_svs` token is present), then calls `nii_saveNII` + `nii_SaveBIDSX` with a tweaked `TDICOMdata` (`isMrsRef = true`, `bidsEntitySuffix = "_mrsref"`). Only the exact 2× case is supported — 3×+ multipliers (dynamics / edit-on-off / multi-coil) are Phase 2.d work. The sidecar's `ScanningSequence "MRSI"` branch (`~3250`) is dead code until the writer learns spatial-dim packing — do not infer MRSI support from the sidecar mapping.

**`WaterSuppressed` sidecar field**: BIDS-MRS requires it on `_svs`/`_mrsi`/`_mrsref`. Emitted from `nii_SaveBIDSX` MRS block (`~3285`) as `!d.isMrsRef` — `true` for the main acquisition, `false` for the water-ref. Don't add another emission site; the gate is the single source of truth.

**Stack validation** in `saveDcm2NiiMRS`: every member must agree with `d0` on `isMRS`, `dataPointColumns`, spectral width (canonical `mrsSpectralWidthHz` value, 1e-6 relative tolerance), `isLittleEndian`, `manufacturer`, `isXA`, orient/position/voxel-size (1e-4 absolute). Affine validity gate: zero/NaN/Inf orient or non-positive voxel spacing → `sform_code=0`. Foreign save formats (MGH / NRRD / BJNIfTI) rejected at dispatch.

**BEP009 PET-array suppression / `initTDTI4D()` contract**: `nii_SaveBIDSX` gates several emissions on `dti4D->X[0] >= 0.0` "unset" sentinels. The BEP009 PET arrays (`DecayCorrectionFactor`, `FrameTimesStart`, `FrameDuration`, `FrameReferenceTime`) plus `SliceTiming`, `IntensityScaleFactor`, and the `RepetitionTime` fallback all read these. Every caller that builds a TDTI4D for nii_SaveBIDSX MUST call `initTDTI4D()` first (file-static helper at `nii_dicom_batch.cpp:~3470`). Skipping it on a `memset(0)` stack local leaks `h->dim[4]`-long zero arrays — for MRS that's the spectral-point axis (typically 1024–2048), not a frame count, so the sidecar grows by tens of KB of meaningless zeros (commit `33da307` was the MRS case; commit-after-that consolidated the four sites with `initTDTI4D`). Do NOT init-by-field at a new TDTI4D site — coverage drifted across four sites before the helper landed.

**Philips oversized payload**: classic Philips SVS packs `nframes × spec_points` in `(5600,0020)`; a common variant trails a water-reference FID. Writer accepts integer-multiple sizes, reads first `bytes_per_dicom` into the main FID, warns once per series (audit L1). For the exact 2× case the trailing chunk is captured into the parallel `fidRef` buffer and emitted as the `<stem>_mrsref.nii(.gz)` companion (see the SVS / `_mrsref` writer block above). 3×+ multipliers (dynamics / edit-on/off / multi-coil) are still dropped — Phase 2.d.

**MRSI negative-evidence gate (audit 2026-06-07 round-3 H2)**: classic Siemens CSI/MRSI files set `Rows` / `Columns` (and optionally `NumberOfFrames`) > 1; `saveDcm2NiiMRS`'s `isSVSConfirmed` predicate at `nii_dicom_batch.cpp:~12231` checks `hasSpatialGrid = xyzDim[1] > 1 || xyzDim[2] > 1 || xyzDim[3] > 1` and demands populated CSA VOI tags before admitting an `mrsAcqType == kMRSAcqNone` file as `_svs`. **Known gap (audit 2026-06-07 H1):** this guard is insufficient in practice — `spec2nii_compare.py --all` still reports `sm_classic`, VB/VE 3D CSI, `voi_in_mrsi`, and `siemens_F3T_voi_in_mrsi` with `BidsGuess _svs` when the expected suffix is `_mrsi`. The `xyzDim` heuristic does not match the actual classic-Siemens CSI dim packing in those cases. Do not loosen the existing guard while a real MRSI writer is missing; do tighten it if you can characterize the missing negative signal. A full MRSI writer + dispatch is Phase 6 work and is the real fix.

**MRS split policy (decision 2026-06-07, post-P2 cycle)**: dcm2niix bundles **raw multi-DICOM / multi-frame MRS series** as a single NIfTI (`dim[5] = total frames`, no reorder, no drop). The existing C-side per-DICOM split criteria (multi-echo `EchoTime`, multi-PLD ASL, coil splitting under `-m o`) all key off **standard, public DICOM tags** that vary per file — those stay in the C path. Vendor-sequence-state-specific reshapes/splits (CMRR sLASER DKD `_rf_off` / `_rf_grads_ovs_off` reference-grouping; Philips MEGA-PRESS edit-on/off + water-ref crop; HYPER edit pairing) live in vendor Phoenix Protocol or per-frame private SQs — that's interpretation, not DICOM semantics, and belongs above the converter. The sibling tool [`tools/mrs_post.py`](tools/mrs_post.py) (round-5 cycle, 2026-06-07) owns these splits: pure pydicom + numpy, takes the dcm2niix bundled NIfTI + source DICOMs, and writes per-group / reshaped outputs that match spec2nii's expected file layout. Direct dcm2niix users continue to get raw bundled NIfTI; users who want spec2nii parity run `tools/mrs_post.py` afterward (or use `dcm_qa_mrs/spec2nii_compare.py --with-mrs-post`).

- **Siemens sLASER (CMRR DKD)**: the Phoenix `alTE[0..N]` summing for total `EchoTime` already landed C-side (`siemensMrsTotalEchoTimeUs` at `nii_dicom_batch.cpp:~11588`), so the bundled output gets the correct TE in filename + sidecar. The multi-DICOM **REF-SCAN ORDERING split** (spec2nii's `identify_integrated_references` — partition frames by `lAutoRefScanMode == 8/2`, `lAutoRefScanNo`, `lAverages`) is in `tools/mrs_post.py` (`_identify_dkd_reference`). Audit-round-5 H1 closed the latent `csaAscii.alTE[]` stack-uninit bug at the helper's call site (`= {}` zero-init at `~11486`).

- **Philips MEGA-PRESS reshape (reverted 2026-06-07 round-4 cycle)**: the `philipsScanMegaPressFrames` byte-scanner + `press_mega (N_pts, n_dyn, 2)` reshape + `dim_6: DIM_EDIT` / `dim_6_header` sidecar block are GONE from the C side. `press_mega` Enhanced DICOMs now land as `(1024, 297)` with all 288 main + 9 reference frames intact — `dim[5] = N_dyn` only, no edit-axis interpretation. `svsWSAntCing` P2.d 32-dynamic stacking is on a separate code path (data-driven byte-multiplier at `nii_dicom_batch.cpp:~11567-11574`) and remains. The one-byte OOB read in the deleted scanner went with it; if you reintroduce per-frame DICOM scanning, do not pattern-replicate `for (long p = 0; p < sz - 8; p++) { ... buf[p+9] ... }` — that loop bound is off by one. MEGA-PRESS edit interpretation now lives in `tools/mrs_post.py` (`reshape_philips_mega`): reads `(2005,1597)` is_edited + per-frame `(2005,1304)` ref-flag + `(2005,1598)` edit ON/OFF, writes `_svs (1024,144,2)` + `_mrsref (1024,9)`.

- **C-side scope going forward**: raw FID layout, affine construction, suffix classification (`_svs` / `_mrsref`), BIDS-MRS required + recommended sidecar fields, Philips classic 2× `_mrsref` companion, single-source-of-truth helpers (`mrsSpectralWidthHz`, `mrsIsStandaloneWaterRef`, `initTDTI4D`). Vendor-state-specific reshapes/splits explicitly out — those live in `tools/mrs_post.py`.

**`tools/mrs_post.py` interface** (round-5 cycle 2026-06-07): `python3 tools/mrs_post.py <bundled.nii> --dicoms <dicom-dir-or-file>`. Three cases auto-detected from sidecar Manufacturer + source DICOM private tags: (a) Siemens sLASER DKD multi-DICOM ref split (mode=8 and mode=2 layouts; mirrors spec2nii `identify_integrated_references`), (b) Philips MEGA-PRESS reshape ON/OFF + ref crop (mirrors spec2nii `_process_philips_svs_new` MEGA branch), (c) `_mrsref` companion sanity check (pass-through validation for the Philips classic 2× case dcm2niix's C side already emits paired). Outputs are written next to the input NIfTI with spec2nii-style filename suffixes (`_svs`, `_svs_rf_off`, `_svs_rf_grads_ovs_off`, `_mrsref`). Sidecar `BidsGuess` stays canonical (`_svs` / `_mrsref`) so downstream BIDS tooling routes correctly. Tested via `dcm_qa_mrs/spec2nii_compare.py --with-mrs-post`: 22/40 PASS (vs 15/40 bare).

Validated against spec2nii XA60 SVS series (`/Users/chris/src/spec2nii/...`): byte-identical FID + sform float32-precise. Per-vendor parity matrix tracked in `spec_plan.md`.

Attribution: ported from spec2nii (BSD-3-Clause, William Clarke, U. Oxford 2020).

## Git Workflow

- **master** — stable releases only, no PRs accepted
- **development** — active development branch, PRs go here
- PRs target `development`, not `master`

## Code Style

```bash
clang-format -i -style="{BasedOnStyle: LLVM, IndentWidth: 4, IndentCaseLabels: false, TabWidth: 4, UseTab: Always, ColumnLimit: 0}" *.cpp *.h
```
