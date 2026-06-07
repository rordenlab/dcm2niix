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

**Phase convention**: NumarisX (XA) negates each odd-indexed (imag) float except `+0.0` (vs spec2nii reference). VE/VX (Numaris4) skips the negation.

**Sidecar required (BIDS-MRS)**: `ResonantNucleus` (array), `SpectrometerFrequency` (array, `%.9g` precision), `SpectralWidth` (`%.17g` precision), `EchoTime`. Recommended: `NumberOfSpectralPoints`, `AcquisitionVoxelSize` (uses `zThick` not `xyzMM[3]`), `NumberOfTransients` (`NumberOfAverages * dim[5]`), `ScanningSequence` constrained to `SVS`/`MRSI`/`Unlocalized MRS`, `DwellTime = 1/SpectralWidth`, `dim_5: DIM_DYN` (always), `TransmitCoilName`, `InversionTime` (emits 0.0 even when no inversion).

**`mrsSpectralWidthHz()`** at `nii_dicom_batch.cpp:~1270` is the single source-of-truth for MRS spectral width, used by both the sidecar (`SpectralWidth`/`DwellTime` emission at `~3180`) and the NIfTI writer (`hdr.pixdim[4]` in `saveDcm2NiiMRS` at `~11618`). Prefers integer-ns Siemens private `(0021,1142)` `RealDwellTime` (`1.0e9 / d.dwellTime`) over CSA-float `d.spectralWidth` — float32 CSA loses ~6 sig figs at typical SVS widths (audit 2026-06-07 H2). Keep the helper canonical: do not re-inline the `dwellTime > 0` gate or compute spectral width directly at either call site.

**SVS / `_mrsref` writer**: `saveDcm2NiiMRS` hard-rejects `mrsAcqType` ROW/PLANE/VOLUME (`nii_dicom_batch.cpp:~11403`). The BIDS-MRS entity suffix is one of:

- `_svs` when `mrsAcqType == kMRSAcqSingleVoxel` OR (`mrsAcqType == kMRSAcqNone` AND CSA VOI tags populated — `zThick > 0` AND `xyzMM[1] > 1.0f` AND `xyzMM[2] > 1.0f`).
- `_mrsref` when the series-naming heuristic (`wrsoff` / `wrs_off` / `no_Water_Suppression` / `noWS` / `_mrsref` substring in `seriesDescription` / `protocolName` / `sequenceName` / `pulseSequenceName`) marks the acquisition as a standalone water reference; flips `d.isMrsRef = true` so the sidecar emits `WaterSuppressed: false`.
- empty (file lands in `Unknown/`) when neither SVS evidence nor water-ref naming applies — guards against classic Siemens CSI/MRSI inputs (audit 2026-06-07 H3).

**`_mrsref` companion (Philips P2.c)**: when every DICOM in the stack carries exactly 2× expected payload bytes (`bytes_per_dicom`), a parallel `fidRef` buffer captures the trailing water-reference FID alongside the main FID. After the main `_svs` NIfTI/sidecar are written, the companion writer derives `<stem>_mrsref.nii(.gz)` by substituting `_svs → _mrsref` in `pathoutname` (or appending `_mrsref` when no `_svs` token is present), then calls `nii_saveNII` + `nii_SaveBIDSX` with a tweaked `TDICOMdata` (`isMrsRef = true`, `bidsEntitySuffix = "_mrsref"`). Only the exact 2× case is supported — 3×+ multipliers (dynamics / edit-on-off / multi-coil) are Phase 2.d work. The sidecar's `ScanningSequence "MRSI"` branch (`~3250`) is dead code until the writer learns spatial-dim packing — do not infer MRSI support from the sidecar mapping.

**`WaterSuppressed` sidecar field**: BIDS-MRS requires it on `_svs`/`_mrsi`/`_mrsref`. Emitted from `nii_SaveBIDSX` MRS block (`~3285`) as `!d.isMrsRef` — `true` for the main acquisition, `false` for the water-ref. Don't add another emission site; the gate is the single source of truth.

**Stack validation** in `saveDcm2NiiMRS`: every member must agree with `d0` on `isMRS`, `dataPointColumns`, spectral width (canonical `mrsSpectralWidthHz` value, 1e-6 relative tolerance), `isLittleEndian`, `manufacturer`, `isXA`, orient/position/voxel-size (1e-4 absolute). Affine validity gate: zero/NaN/Inf orient or non-positive voxel spacing → `sform_code=0`. Foreign save formats (MGH / NRRD / BJNIfTI) rejected at dispatch.

**Philips oversized payload**: classic Philips SVS packs `nframes × spec_points` in `(5600,0020)`; a common variant trails a water-reference FID. Writer accepts integer-multiple sizes, reads first `bytes_per_dicom` into the main FID, warns once per series (audit L1). For the exact 2× case the trailing chunk is captured into the parallel `fidRef` buffer and emitted as the `<stem>_mrsref.nii(.gz)` companion (see the SVS / `_mrsref` writer block above). 3×+ multipliers (dynamics / edit-on/off / multi-coil) are still dropped — Phase 2.d.

**MRSI negative-evidence gate (audit 2026-06-07 round-3 H2)**: classic Siemens CSI/MRSI files set `Rows` / `Columns` (and optionally `NumberOfFrames`) > 1; `saveDcm2NiiMRS`'s `isSVSConfirmed` predicate at `nii_dicom_batch.cpp:~11815` rejects them via `hasSpatialGrid = xyzDim[1] > 1 || xyzDim[2] > 1 || xyzDim[3] > 1` even when CSA VOI tags are populated. Do not loosen this guard while MRSI lacks a writer; it's the only thing stopping `sm_classic` / VB-VE 3D CSI / `voi_in_mrsi` from being silently mislabeled as singleton `_svs`.

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
