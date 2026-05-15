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

Wider regression at [`dcm_validate`](https://github.com/neurolabusc/dcm_validate) (~35 `dcm_qa_*` submodules). Run before releases or when a change touches vendor-specific code the in-tree trio doesn't cover. **Read each `batch.sh` before running** — they vary in `-f` format, zip/gz handling, and `dcm_qa_sag` is a single-file `dtifits.py` check rather than a Ref diff.

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
- **reproin.cpp / reproin.h** — ReproIn one-pass filename emulation invoked from the `%H` branch in `nii_dicom_batch.cpp`. Long-form grammar, precedence, and known limitations live in [REPROIN.md](REPROIN.md)

Bundled libraries (no external install needed): miniz (zlib), cJSON, NanoJPEG, CharLS (in `console/charls/`).

### Data flow
1. `main_console.cpp` parses CLI args into `TDCMopts` struct
2. `nii_dicom_batch.cpp` scans input directory, calls `nii_dicom.cpp` to parse each DICOM file
3. `nii_dicom.cpp` populates `TDICOMdata` with metadata and decompresses pixel data if needed
4. `nii_dicom_batch.cpp` groups files by series, assembles volumes
5. `nifti1_io_core.cpp` writes NIfTI files and JSON sidecars

### Sequence-filter quirks in `nii_dicom.cpp`

Several DICOM sequences are "filtered out" during parsing because their contents are reference/historical data that would corrupt the main header (issues #599, #655, #639):
- `(0400,0561) OriginalAttributesSequence` — tracked by `sqDepth04000561`. Issue #989 fix: peek at the raw 4-byte SQ length and skip latching when length is 0; explicit-length-0 SQs (anonymized DICOMs) never produce item delimiters, so the latch would otherwise never clear and every subsequent tag would be silently dropped.
- `(0008,9092) ReferencedImageEvidenceSequence` — tracked by `is00089092SQ` (boolean, cleared on any unNest — crude but OK because this SQ is small and shallow).
- `(0088,0200) IconImageSequence` — tracked by `sqDepthIcon`.
- `(0054,0016) RadiopharmaceuticalInformationSequence` — tracked by `is00540016SQ`. Used to scope nested `CodeMeaning (0008,0104)` lookups (issue #983); the **first** `CodeMeaning` encountered inside wins, so `RadionuclideCodeSequence` beats `RadiopharmaceuticalCodeSequence`.

### UIH MOSAIC bvec

UIH DICOMs may store diffusion gradient directions in both the standard `(0018,9089) DiffusionGradientOrientation` tag and the private `(0065,1037)` tag. The private tag is more reliable for UIH MOSAIC data. When `manufacturer == UIH`, always prefer `(0065,1037)` regardless of which tag appears first in the file (issue #993, commit `ab6f48c`). Do not "optimize" this by skipping the private tag when the standard tag is present.

### Implicit-VR sequence descent

`isSQ()` in `nii_dicom.cpp` is the **explicit allowlist** of SQ tags the implicit-VR parser will recurse into. Adding a tag here makes the parser descend; omitting it means the entire SQ is treated as an opaque blob and any nested tags are invisible. PET tags `(0054,0016)`, `(0054,0300)`, `(0054,0304)` were added recently for issue #983 so radionuclide/tracer code sequences are reachable on implicit-VR datasets.

### High-slice volumes (>`kMaxEPI3D`)

`kMaxEPI3D` (in `nii_dicom.h`) caps the per-volume slice-timing arrays (`CSA.sliceTiming[]`). Volumes with `hdr->dim[3] > kMaxEPI3D` must still receive orientation/affine finalisation — commit `1cd1620` fixed a regression where the high-slice branch early-returned out of `headerDcm2Nii2()` and produced wrong orientation. Do **not** reintroduce an early return there. Slice-timing-specific code paths (`sliceTimingGE`, `allSame` loops) still assume `dim[3] <= kMaxEPI3D` — gate any new slice-timing work on that bound, but keep orientation logic running unconditionally.

### Filename format specifiers (`-f` flag)

Full list in [FILENAMING.md](FILENAMING.md). Two conventions are case-sensitive and easy to confuse:
- `%v` = vendor full name (`Canon`, `Siemens`, `GE`); `%m` = 2-char abbreviation (`Ca`, `Si`, `GE`).
- `%h` = legacy hazardous BIDS hierarchical naming (unchanged). `%H` = ReproIn one-pass emulation (see below + [REPROIN.md](REPROIN.md)).

### ReproIn one-pass filenames (`-f H` / `%H`)

`%H` parses `(0018,1030) ProtocolName` (fallback `(0008,103e) SeriesDescription`) into a BIDS stem and uses `(0008,1030) StudyDescription` (fallback `(0040,0254) PerformedProcedureStepDescription`) as the path prefix. All logic is in `console/reproin.cpp`; the `f == 'H'` branch in `nii_dicom_batch.cpp` only dispatches. The legacy `%h` path is untouched. Defaults match heudiconv: `sub-` is derived from `PatientID` via `reproinFixupSubjectId`, and `ses-` is **omitted** unless the protocol or `reproinResolveSession` (`_ses-{date}` / `_ses-DATE` against `dcm.studyDate`) provides one. Companion flags: `-bi` overrides subject, `-bv` overrides session, `-br` overrides the project subdirectory (default = StudyDescription; `-br .` suppresses it so `-o` is the BIDS root). CLI values and entities are sanitised against `..`/separator injection before path use.

Cross-series concerns (fmap pairing via ShimSetting + NIfTI affine, `_scans.tsv`, `B0FieldIdentifier`/`B0FieldSource`, session backfill, BIDS root scaffolding) are handled by `tools/reproinx.py` — stdlib-only, parses NIfTI-1 headers directly with `gzip`/`struct`. Flags: `--anonymize` (suppress `-ba n`), `--strict` (fail-fast). Full grammar, defaults, privacy notes, and the complete limitations list live in [REPROIN.md](REPROIN.md) — keep that document in sync when touching `reproin.cpp` or `tools/reproinx.py`.

The post-pass must stay non-destructive on existing curated metadata: cleanup deletions (e.g. dropping `task-<X>_bold.json` once an `_acq-` variant is emitted, or `README.md` once a plain `README` exists) only proceed when the file content matches the known dcm2niix stub shape (`_is_dcm2niix_task_stub`, `_is_dcm2niix_readme_stub`). Hand-written sidecars and READMEs must survive re-runs untouched.

## Git Workflow

- **master** — stable releases only, no PRs accepted
- **development** — active development branch, PRs go here
- PRs should target `development`, not `master`

### Pre-push checks

Before pushing a commit that touches conversion logic, run at minimum:

1. The in-tree `dcm_qa` regression suite (vendor-agnostic core):
   ```bash
   /regressiontest                       # tests build/bin/dcm2niix by default
   # or, manually:
   (cd dcm_qa && ./batch.sh)
   ```
   `/regressiontest` runs all three of `dcm_qa`, `dcm_qa_nih`, `dcm_qa_uih`. For
   a faster gate, just running `dcm_qa` catches most cross-vendor breakage.
2. `codespell` (matches the `Codespell` GitHub Action). The CI fires on every
   push to `development`/`master` and every PR — failures block merge.
   ```bash
   git ls-files | xargs codespell        # mimics CI scope (skips build/, node_modules/)
   ```
   Configuration lives in `.codespellrc`. Domain terms that shouldn't be
   flagged (e.g. Siemens `PULS` for the pulse-oximeter waveform) belong in its
   `ignore-words-list` with a comment explaining the term — don't disable
   codespell for the path.

## Code Style

Formatted with clang-format:
```bash
clang-format -i -style="{BasedOnStyle: LLVM, IndentWidth: 4, IndentCaseLabels: false, TabWidth: 4, UseTab: Always, ColumnLimit: 0}" *.cpp *.h
```
