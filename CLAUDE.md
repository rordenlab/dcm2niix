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

## Build Commands

### Quick build (CMake, recommended for full features):
```bash
mkdir build && cd build
cmake -DZLIB_IMPLEMENTATION=Cloudflare -DUSE_JPEGLS=ON -DUSE_OPENJPEG=ON ..
make
```
Output binary: `build/bin/dcm2niix`

### Simple build (Make, no optional decoders):
```bash
cd console && make
```

### Makefile targets (from console/):
- `make` — optimized release build
- `make debug` — debug symbols, no optimization
- `make sanitize` — AddressSanitizer build
- `make jp2` — with OpenJPEG (JPEG2000)
- `make turbo` — with TurboJPEG (lossy JPEG)
- `make wasm` — WebAssembly build

### Key CMake options:
- `-DZLIB_IMPLEMENTATION=Miniz|System|Cloudflare` — compression backend (default: Miniz bundled)
- `-DUSE_OPENJPEG=OFF|GitHub|System` — JPEG2000 decompression
- `-DUSE_JPEGLS=ON|OFF` — JPEG-LS decompression (CharLS, bundled in console/charls/)
- `-DUSE_TURBOJPEG=ON|OFF` — lossy JPEG via libjpeg-turbo
- `-DUSE_ZSTD=ON|OFF` — Zstandard output compression (`-z s`, requires libzstd)
- `-DBATCH_VERSION=ON` — build dcm2niibatch (requires yaml-cpp)

## Testing

### In-tree minimum regression suite

Three regression submodules ship with this repo and must be run before every non-trivial commit:

- `dcm_qa` — multi-vendor core set
- `dcm_qa_nih` — NIH-contributed acquisitions
- `dcm_qa_uih` — United Imaging Healthcare (UIH) data

Each has `In/` (DICOM input), `Ref/` (reference NIfTI + JSON), and a `batch.sh` that runs dcm2niix and diffs `Ref/` against fresh `Out/`. The diff ignores `ConversionSoftwareVersion` and `BidsGuess`. `batch.sh` uses `set -eu`; a non-zero exit means output drifted and the commit should be investigated.

Run all three:
```bash
# inside Claude Code
/regressiontest                 # uses build/bin/dcm2niix by default
/regressiontest /abs/path/bin   # test a specific binary

# or manually
git submodule update --init
export PATH="$PWD/build/bin:$PATH"
for d in dcm_qa dcm_qa_nih dcm_qa_uih; do
    (cd "$d" && ./batch.sh) || { echo "FAIL: $d"; exit 1; }
done
```

A passing run of all three submodules is the baseline expectation before any commit touching conversion logic.

### Full vendor matrix (release gate)

The wider regression suite lives in [`dcm_validate`](https://github.com/neurolabusc/dcm_validate) (~35 `dcm_qa_*` submodules). Run this before cutting a release or when a change touches vendor-specific code paths that the in-tree trio doesn't cover. Check the sibling path `../dcm_validate` first; if absent, clone with submodules:
```bash
git clone --recursive https://github.com/neurolabusc/dcm_validate.git ../dcm_validate
```
**Read each `batch.sh` before running it** — they vary widely: different `-f` formats, some unzip `In/*.zip` (and delete the originals), some decompress `Ref/*.nii.gz` in place, some iterate subfolders, and `dcm_qa_sag` is intentionally a single-file `dtifits.py` validation rather than a Ref diff.

For ad-hoc smoke tests against one folder:
```bash
./dcm2niix -v 2 ../dcm_qa/
```

## Architecture

All source is in `console/`. Key files:

- **main_console.cpp** — entry point, CLI argument parsing, orchestrates conversion
- **nii_dicom.cpp** (~8.8k lines) — DICOM parser. Reads headers, extracts metadata into `TDICOMdata` struct, handles all vendor-specific tag parsing. This is the core of the project
- **nii_dicom.h** — defines `TDICOMdata` (280+ fields), `TDTI4D` (diffusion/4D params), `TCSAdata` (Siemens CSA). All key data structures live here
- **nii_dicom_batch.cpp** — batch processing: groups DICOMs into series, merges slices into 3D/4D volumes, assembles the final NIfTI
- **nifti1_io_core.cpp** — NIfTI writer, BIDS JSON sidecar generation, image reorientation
- **nii_foreign.cpp** — non-DICOM format support (Philips PAR/REC)
- **nii_ortho.cpp** — slice reorientation, cropping, resampling
- **jpg_0XC3.cpp** — lossless JPEG decoder for DICOM transfer syntax
- **ujpeg.cpp** — bundled NanoJPEG (lossy JPEG fallback)

Bundled libraries (no external install needed): miniz (zlib), cJSON, NanoJPEG, CharLS (in `console/charls/`).

### Data flow
1. `main_console.cpp` parses CLI args into `TDCMopts` struct
2. `nii_dicom_batch.cpp` scans input directory, calls `nii_dicom.cpp` to parse each DICOM file
3. `nii_dicom.cpp` populates `TDICOMdata` with metadata and decompresses pixel data if needed
4. `nii_dicom_batch.cpp` groups files by series, assembles volumes
5. `nifti1_io_core.cpp` writes NIfTI files and JSON sidecars

### Feature macros (preprocessor)
`myEnableJPEGLS`, `myTurboJPEG`, `myEnableJasper`, `myDisableOpenJPEG`, `myEnableJNIFTI`, `myEnableZSTD`, `myDisableMiniZ`, `myDisableClassicJPEG`, `LINKING_FREESURFER`

### Sequence-filter quirks in `nii_dicom.cpp`

Several DICOM sequences are "filtered out" during parsing because their contents are reference/historical data that would corrupt the main header (see issues #599, #655, #639):
- `(0400,0561) OriginalAttributesSequence` — tracked by `sqDepth04000561` (depth-aware)
- `(0008,9092) ReferencedImageEvidenceSequence` — tracked by `is00089092SQ` (boolean, cleared on any unNest — crude but generally OK because this SQ is small and shallow)
- `(0088,0200) IconImageSequence` — tracked by `sqDepthIcon`
- `(0054,0016) RadiopharmaceuticalInformationSequence` — tracked by `is00540016SQ` (boolean, same crude unNest pattern as `is00089092SQ`). Used to scope nested `CodeMeaning (0008,0104)` lookups for radionuclide/tracer naming (issue #983); the **first** `CodeMeaning` encountered inside wins, so `RadionuclideCodeSequence` is picked over `RadiopharmaceuticalCodeSequence`.

### UIH MOSAIC bvec

UIH DICOMs may store diffusion gradient directions in both the standard `(0018,9089) DiffusionGradientOrientation` tag and the private `(0065,1037)` tag. The private tag is more reliable for UIH MOSAIC data. When `manufacturer == UIH`, always prefer `(0065,1037)` regardless of which tag appears first in the file (issue #993, commit `ab6f48c`). Do not "optimize" this by skipping the private tag when the standard tag is present.

### Implicit-VR sequence descent

`isSQ()` in `nii_dicom.cpp` is the **explicit allowlist** of SQ tags the implicit-VR parser will recurse into. Adding a tag here makes the parser descend; omitting it means the entire SQ is treated as an opaque blob and any nested tags are invisible. PET tags `(0054,0016)`, `(0054,0300)`, `(0054,0304)` were added recently for issue #983 so radionuclide/tracer code sequences are reachable on implicit-VR datasets.

### Filename format specifiers (`-f` flag)

Full list in [FILENAMING.md](FILENAMING.md). Two conventions worth flagging here because they are case-sensitive and easy to confuse:
- `%v` = vendor full name (`Canon`, `Siemens`, `GE`); `%m` = 2-char abbreviation (`Ca`, `Si`, `GE`). Used by many `dcm_qa_*` `batch.sh` scripts — don't conflate them.
- `%h` (lowercase) = hazardous BIDS hierarchical naming; `%H` (uppercase) = hazardous + reproin (uses `studyDescription` as path prefix).

### PET / BIDS notes

`TimeZero` in the BIDS PET sidecar must always equal `SeriesTime`, never `AcquisitionTime`. For delayed reconstructions (or any series where acquisition trails the injection clock), using `AcquisitionTime` desynchronizes `TimeZero` from frame timing and breaks downstream PET pipelines (issue #983).

`ImageDecayCorrectionTime` has two branches keyed off `(0054,1102) DecayCorrection`: `START` emits 0 (corrected to series start = TimeZero); `ADMIN` emits `RadiopharmaceuticalStartTime - TimeZero` (corrected to injection time, expressed relative to TimeZero per BIDS). `NONE` emits nothing.

`TracerName` comes from `(0018,0031) Radiopharmaceutical` (often nested inside `RadiopharmaceuticalInformationSequence`). GE packs this as `"FDG -- fluorodeoxyglucose"` (short name + long description separated by `" -- "`); the parser strips at `" -- "` to emit only the short tracer name. Do not remove this stripping — GE PET data depends on it for clean BIDS `TracerName` values.

`AcquisitionDuration` sources DICOM `(0018,9073)` and is emitted unconditionally for non-UIH data. It is **not** subject to BIDS mutual-exclusion with `RepetitionTime` — that constraint applies only to `FrameAcquisitionDuration` (DICOM `(0018,9220)`, BIDS key of the same name), which dcm2niix does not currently emit. Do not re-introduce a `d.TR <= 0.0` gate here (issue #991, PR #1010 discussion).

`FrameTimesStart` for a static PET reconstruction (`h->dim[4] == 1`) is computed as `AcquisitionTime − SeriesTime` in seconds, clamped at 0. Dynamic (multi-volume) scans use the per-frame onset array from `dti4D->volumeOnsetTime`. Do not hardcode `[0]` for static scans — that silently loses the acquisition offset when reconstruction trails `SeriesTime` (e.g., Philips Gemini, ~42 s offset; upstream PR #1010).

`AttenuationCorrection` (BIDS spelling) and `AttenuationCorrectionMethod` (DICOM spelling) share the same source `d.attenuationCorrectionMethod` but are **not interchangeable**. `AttenuationCorrectionMethod` keeps the verbatim string; `AttenuationCorrection` is emitted as the pre-comma token. Siemens Biograph packs `"measured,AC_CT_Brain"` in `(0054,1101)`; BIDS wants just `"measured"`. The duplicate-looking `json_Str` emits are intentional — do not collapse them.

**Philips ASL volume order**: For Philips ASL data (label/control pairs, multi-phase, 3D pCASL Sources), volumes are reordered into **temporal acquisition order** rather than the scanner's logical/storage order. This is intentional per [issue #533](https://github.com/rordenlab/dcm2niix/issues/533) (commit `66a4fd0`). When updating Ref files for `dcm_qa_philips*` datasets, expect volume reshuffles — same set of voxels, different volume indexing.

### Siemens physio (XA PhysioLogging + legacy CMRR PMU)

dcm2niix extracts two distinct Siemens physio-in-DICOM formats, both carried at private tag `(7FE1,1010)`:

1. **XA30/XA60 PhysioLogging** — Raw Data Storage SOP (`1.2.840.10008.5.1.4.1.1.66`) with a gzipped XML payload (gzip magic `1F 8B`).
2. **Legacy CMRR Multi-Band (VE11C) PMU** — Siemens CSA Non-Image Storage SOP (`1.3.12.2.1107.5.9.1`) with a raw binary blob: one 1024-byte padded header per waveform followed by ASCII Siemens PMU log lines.

Detection in `nii_dicom.cpp` (parser case `kSiemensXAPhysio`) is gated on `d.isRawDataStorage`. The CSA Non-Image SOP `1.3.12.2.1107.5.9.1` was added to the `isRawDataStorage` allowlist specifically because legacy CMRR PMU lives there; **MR Spectroscopy DICOMs also use this SOP**, so the CMRR branch additionally requires the per-waveform header `fname` field to contain a known PMU log suffix (`_PULS.log`, `_RESP.log`, `_EXT.log`, `_ECG.log`, or `_Info.log`) before setting `d.isCMRRPhysio`. The XA branch peeks for gzip magic bytes. The two flags are mutually exclusive. Note that `d.isValid = true` is deliberately set for both so the file survives the dispatcher's image-validity filter and reaches the hook — do not "fix" this.

The dispatch hook lives at the top of `saveDcm2NiiCore` in `nii_dicom_batch.cpp`, branching on `isXAPhysio` vs `isCMRRPhysio` to call `xaPhysioConvert` or `cmrrPhysioConvert`; either short-circuits the NIfTI machinery entirely and returns. Both formats then funnel through the shared `physioBidsFillUniform` (uniform-grid resampling with NaN fill for sparse streams such as EXT trigger pulses, written as the literal `nan` to match bidsphysio) and `xaPhysioWriteStreamFiles` write path, so output is identical schema regardless of source: BIDS `_recording-<label>_physio.tsv.gz` plus `.json` (PULS→cardiac, RESP→respiratory, ECG→ecg, EXT→external_trigger). `StartTime` is typically negative because PMU recording is started manually before the scan, and the volume timeline is rasterised onto each stream's trigger column.

Trigger placement uses **ceil semantics** to match bidsphysio's `argmax(times >= t)`; `StartTime` is **millisecond-truncated** to match bidsphysio's `int(t_start_ms) / 1000`. Output is byte-equivalent to the [cbinyu/bidsphysio](https://github.com/cbinyu/bidsphysio) Python parser on both XA and CMRR fixtures, modulo a 1-millisecond `StartTime` divergence on sparse streams where bidsphysio loses precision via floating-point arithmetic — dcm2niix's integer-tic arithmetic is exact here. bidsphysio remains the canonical Python equivalent for richer cases; the in-converter shortcut produces equivalent output for the common path so users don't have to round-trip through a second tool.

**Known risk:** These latches assume the parser will encounter item delimiters to unlatch. An **empty** sequence (explicit length 0, common in anonymized DICOMs) has no items, so the latch never clears and every subsequent tag is silently dropped. Fix for issue #989 peeks at the raw 4-byte SQ length at `case kOriginalAttributesSq` and skips latching when the length is 0.

**Re-evaluate this fix if:**
- A vendor or anonymizer ships a populated `(0400,0561)` with an *explicit non-zero* length that we incorrectly enter (the current `lLength > 8` guard already covers most cases, but explicit-length SQs under 8 bytes could regress).
- A regression surfaces where fields *inside* `(0400,0561)` now leak into the header — that would mean the empty-check is misfiring on a non-empty SQ, likely due to implicit-VR data where the byte-peek is unsafe.
- We add support for sequences that genuinely *need* the filter to persist across an empty SQ (none known today).

## Git Workflow

- **master** — stable releases only, no PRs accepted
- **development** — active development branch, PRs go here
- PRs should target `development`, not `master`

## Code Style

Formatted with clang-format:
```bash
clang-format -i -style="{BasedOnStyle: LLVM, IndentWidth: 4, IndentCaseLabels: false, TabWidth: 4, UseTab: Always, ColumnLimit: 0}" *.cpp *.h
```
