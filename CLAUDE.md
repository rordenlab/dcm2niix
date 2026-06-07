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

### Session scratch: `temp/`

`temp/` at the repo root is session scratch — DICOM samples, dcmdump output, and JSON sidecars dropped here during `/audit`, regression triage, and ad-hoc analysis. Gitignored (`/temp/` in `.gitignore`) so it cannot be committed, but **the gitignore is defence in depth, not a privacy fix**: the data on disk still bears PHI from live scans, and a hidden directory can mask sensitive material from normal `git status` review.

Retention rule: clean `temp/` (e.g. `rm -rf temp/`) at the end of every analysis cycle that placed files there — do not let it accumulate across unrelated sessions. If the user has not explicitly authorised retention, an audit-time cleanup is correct. The convention exists so `/audit` agents and the user share one drop zone; it does not exempt the contents from privacy scrutiny.

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

### macOS builds use 16 MB stack (issue #867)

The macOS `console/makefile` build uses `-O3 -flto`; the cmake build uses `-O3` only. LTO inlines aggressively and inflates per-function stack frames (`readDICOMx` already ~1.5 MB even without LTO). On multi-study Siemens XA datasets — multiple Enhanced-DICOM series, deident sequences, dense diffusion stacks — the conversion chain on the makefile build exceeded the 8 MB default macOS main-thread stack and tripped `___chkstk_darwin` at `0x16f603ff8`. Repro: a real-world XA80 dataset surfaced by an external user (multiple study dates under one input root, ~100 series). The cmake build does NOT crash on the same data today, but as the corpus widens further (deident, dense DTI, MRS — the recent additions that already chew through the budget) it could; matching the flag closes the gap pre-emptively.

Mitigation: `LFLAGS=-Wl,-stack_size -Wl,0x1000000` in [console/makefile](console/makefile) (restored for issue #867) AND a matching `target_link_options(... "-Wl,-stack_size" "-Wl,0x1000000")` in [console/CMakeLists.txt](console/CMakeLists.txt) (commit 68f6181) double the stack to 16 MB. The cmake gate is `if (APPLE AND (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"))` — without the `AppleClang` clause the block silently skipped on stock macOS Xcode (Apple's clang reports `AppleClang`, not `Clang`). Both builds otherwise produce byte-identical NIfTI / JSON output.

Per-frame stack-pressure reductions in the C source (heap-allocated `deID_CS`, etc.) remain in place — the 16 MB stack is headroom for LTO inlining specifically. If a future audit finds further frame growth, prefer source-side reduction over raising the stack again.

### TDICOMdata size is load-bearing — do not grow inline arrays

`TDICOMdata` is passed BY VALUE through five functions in the save chain (`saveDcm2NiiCore → nii_loadImgXL → headerDcm2Nii → headerDcm2Nii2 → headerDcm2NiiSForm`), and `readDICOMx` itself allocates `1.5 MB` of stack (mostly the many scattered `char txt[1024]` scratch buffers in its 4600-line body). At ~9.6 KB per struct the chain just fits the macOS 8 MB main-thread stack; growing the struct even by ~4 KB tips `headerDcm2NiiSForm`'s prologue probe (`___chkstk_darwin`) past the guard page and crashes.

If a new per-file payload needs storage on `TDICOMdata` (the natural place, since `dti4D` is reused across all files in the parse loop and won't retain per-file data), use a **heap-allocated pointer** with lazy `calloc` and a `free_TDICOMdata_*` helper called before every `free(dcmList)` site. Pattern (see `deID_CS` for the worked example, added for issue #877):

```c
// in struct TDICOMdata
struct TDeIDCodeSequence *deID_CS;  // NULL when unused; 8 bytes vs 4540 inline

// in clear_dicom_data() (or readDICOMx top)
d.deID_CS = NULL;

// in parse: allocate on first hit
if (d.deID_CS == NULL)
    d.deID_CS = (struct TDeIDCodeSequence *)calloc(MAX_DEID_CS, sizeof(struct TDeIDCodeSequence));

// in nii_dicom.cpp: idempotent free helper
void free_TDICOMdata_deID_CS(struct TDICOMdata *d) { /* checks NULL, frees, NULLs */ }

// before every free(dcmList) in nii_dicom_batch.cpp:
for (int i = 0; i < (int)nameList.numItems; i++)
    free_TDICOMdata_deID_CS(&dcmList[i]);
```

The previous implementation of `DeidentificationMethodCodeSequence` re-read the source DICOM via `readDICOMv` inside `nii_SaveBIDSX` to recover the strings, which added `readDICOMx`'s 1.5 MB frame deep in the save chain and crashed on real deident data (`dcm_validate/dcm_qa_deident`, `dcm_qa_philips`). The pointer approach keeps the struct size unchanged and reads the strings directly. Don't reintroduce inline arrays even when they "feel cleaner" — the stack budget has no headroom.

### Sequence-filter quirks in `nii_dicom.cpp`

Several DICOM sequences are "filtered out" during parsing because their contents are reference/historical data that would corrupt the main header (issues #599, #655, #639):
- `(0400,0561) OriginalAttributesSequence` — tracked by `sqDepth04000561`. Issue #989 fix: peek at the raw 4-byte SQ length and skip latching when length is 0; explicit-length-0 SQs (anonymized DICOMs) never produce item delimiters, so the latch would otherwise never clear and every subsequent tag would be silently dropped.
- `(0008,9092) ReferencedImageEvidenceSequence` — tracked by `is00089092SQ` (boolean, cleared on any unNest — crude but OK because this SQ is small and shallow).
- `(0088,0200) IconImageSequence` — tracked by `sqDepthIcon`.
- `(0054,0016) RadiopharmaceuticalInformationSequence` — tracked by `is00540016SQ`. Used to scope nested `CodeMeaning (0008,0104)` lookups (issue #983); the **first** `CodeMeaning` encountered inside wins, so `RadionuclideCodeSequence` beats `RadiopharmaceuticalCodeSequence`.

The `kCodeMeaning` handler at `nii_dicom.cpp:~5981` carries a commented-out `isLocalizer` detector keyed on the literal string `"LOCALIZER"`. Do **not** enable it: many non-scout acquisitions inherit `CodeMeaning` values containing "Localizer" via referenced-image sequences, so the gate produces false positives. Siemens XA localizers are detected via the private tag `(0021,103F) kAutoAlignData` instead (see below).

### Siemens XA localizer detection

`kAutoAlignData (0021,103F)` in `nii_dicom.cpp:~7182` is a Siemens-XA private UT tag whose value (e.g. `"Head_Localizer"`) is upper-cased and tested for `"LOCALIZER"` to set `d.isLocalizer`. Gated on `kMANUFACTURER_SIEMENS` because the tag number is private and could mean something else on other vendors. The case-insensitive match is deliberate — Siemens does not document the casing across XA10/XA30/XA60 firmware. Preferred over `(0008,0104) CodeMeaning` because the latter false-positives on referenced-image inheritance.

### UIH MOSAIC bvec

UIH DICOMs may store diffusion gradient directions in both the standard `(0018,9089) DiffusionGradientOrientation` tag and the private `(0065,1037)` tag. The private tag is more reliable for UIH MOSAIC data. When `manufacturer == UIH`, always prefer `(0065,1037)` regardless of which tag appears first in the file (issue #993, commit `ab6f48c`). Do not "optimize" this by skipping the private tag when the standard tag is present.

### Implicit-VR sequence descent

`isSQ()` in `nii_dicom.cpp` is the **explicit allowlist** of SQ tags the implicit-VR parser will recurse into. Adding a tag here makes the parser descend; omitting it means the entire SQ is treated as an opaque blob and any nested tags are invisible. PET tags `(0054,0016)`, `(0054,0300)`, `(0054,0304)` were added recently for issue #983 so radionuclide/tracer code sequences are reachable on implicit-VR datasets.

### High-slice volumes (>`kMaxEPI3D`)

`kMaxEPI3D` (in `nii_dicom.h`) caps the per-volume slice-timing arrays (`CSA.sliceTiming[]`). Volumes with `hdr->dim[3] > kMaxEPI3D` must still receive orientation/affine finalisation — commit `1cd1620` fixed a regression where the high-slice branch early-returned out of `headerDcm2Nii2()` and produced wrong orientation. Do **not** reintroduce an early return there. Slice-timing-specific code paths (`sliceTimingGE`, `allSame` loops) still assume `dim[3] <= kMaxEPI3D` — gate any new slice-timing work on that bound, but keep orientation logic running unconditionally.

### Filename format specifiers (`-f` flag)

Full list in [FILENAMING.md](FILENAMING.md). Two conventions are case-sensitive and easy to confuse:
- `%v` = vendor full name (`Canon`, `Siemens`, `GE`); `%m` = 2-char abbreviation (`Ca`, `Si`, `GE`).
- `%h` = legacy hazardous BIDS hierarchical naming. `%H` = ReproIn one-pass emulation (see below + [REPROIN.md](REPROIN.md)).

`%h` subject/session defaults at `nii_dicom_batch.cpp:~4210`: `sub-` is taken from `-bi` when set, else from `dcm.patientID` via `reproinFixupSubjectId` (same heudiconv lowercase + strip-`-_` rule used by `%H`), else literal `"1"` (the historical fallback). `ses-` is taken from `-bv` when set, else assembled as `YYYYMMDDTHHMMSS` from `dcm.studyDate` + first 6 chars of `dcm.studyTime` (ISO 8601 compact; the `T` separator keeps the label alphanumeric — BIDS forbids `-`/`_` in session labels), else literal `"1"`. After `reproinx.py` runs its Unknown/-rescue pass (see below), `-f %h` and `-f %H` produce equivalent validator-clean output trees; they still differ at the raw-dcm2niix level (`%h` writes to `Unknown/` more often, `%H` uses ReproIn parsing on `ProtocolName`).

### ReproIn one-pass filenames (`-f H` / `%H`)

`%H` parses `(0018,1030) ProtocolName` (fallback `(0008,103e) SeriesDescription`) into a BIDS stem and uses `(0008,1030) StudyDescription` (fallback `(0040,0254) PerformedProcedureStepDescription`) as the path prefix. All logic is in `console/reproin.cpp`; the `f == 'H'` branch in `nii_dicom_batch.cpp` only dispatches. The legacy `%h` path is untouched. Defaults match heudiconv: `sub-` is derived from `PatientID` via `reproinFixupSubjectId`, and `ses-` is **omitted** unless the protocol or `reproinResolveSession` (`_ses-{date}` / `_ses-DATE` against `dcm.studyDate`) provides one. Companion flags: `-bi` overrides subject, `-bv` overrides session, `-br` overrides the project subdirectory (default = StudyDescription; `-br .` suppresses it so `-o` is the BIDS root). CLI values and entities are sanitised against `..`/separator injection before path use.

Cross-series concerns (fmap pairing via ShimSetting + NIfTI affine, `_scans.tsv` with operator/randstr columns and CRLF line endings, `B0FieldIdentifier`/`B0FieldSource`, session backfill, zero-padded `__dup-NN` renaming, BIDS root scaffolding including `participants.tsv` with `participant_id, age, sex, group`) are handled by `tools/reproinx.py` — stdlib-only, parses NIfTI-1 headers directly with `gzip`/`struct`. Flags: `--anonymize` (upgrades the inner `dcm2niix` call from `-ba o` to `-ba y`), `--strict` (fail-fast), `--keep-derivatives` (retain the scratch `derivatives/scanner/` tree dcm2niix uses for scouts + DERIVED-flagged data — deleted by default to match heudiconv's output), `--no-convert` (skip dcm2niix and only run the post-pass). Full grammar, defaults, privacy notes, and the complete limitations list live in [REPROIN.md](REPROIN.md) — keep that document in sync when touching `reproin.cpp` or `tools/reproinx.py`.

Non-ReproIn study-root collapse (`_maybe_collapse_nonreproin_root`): dcm2niix `-f %H` writes to `<out>/<StudyDescription>/...` and `reproinPathify` in `console/reproin.cpp` splits spaces and underscores in StudyDescription into path separators. So `StudyDescription = "Smith_Aging"` produces the useful `<out>/Smith/Aging/sub-X/` two-level grouping (mirroring the heudiconv reproin `<locator>/<study>` convention; see reference output in `/Users/chris/src/reproin/reproout_*`). For a non-reproin user whose StudyDescription is just a descriptive label or a space-separated duplicate (e.g. `"Studyname Studyname"` → `Studyname/Studyname/`), the hierarchy is content-free filler. The collapse pass detects this by inspecting `OutputStem` in the provenance: if NO row starts with `sub-` (the one-pass ReproIn writer's prefix), no series benefited from the hierarchy and the contents are moved up to `out_root`. Rows under `Unknown/` and `derivatives/scanner/` are silent about reproin discipline so neither blocks the collapse. Safety: only one provenance TSV may exist below `out_root` (multi-study trees keep their grouping), and every level from `out_root` down to the study root must be single-child (avoids clobbering sibling content). Runs before all other passes so subsequent `rglob`-based discovery sees the final paths; README/`dataset_description.json` scaffolding then lands directly at `out_root`.

Unknown/-rescue pre-pass: `_rescue_unknown_dir` walks `<bids_root>/Unknown/` (found via `rglob` so studies where every series landed there are still discovered), reads each JSON sidecar's `BidsGuess`, and promotes the file family (`.nii[.gz]`, `.json`, `.bvec`, `.bval`) to `sub-<heudiconv(PatientID)>/ses-<YYYYMMDDTHHMMSS>/<datatype>/`. Subject/session are derived from the provenance TSV row matching `(StudyInstanceUID, SeriesNumber)` using the new PatientID/StudyDate/StudyTime columns — so this is a no-op under `-ba y` (those columns are absent) and under `--no-convert` against a tree with no provenance. `BidsGuess[0]` of `discard`/`derived` is skipped (the C side already routed those). `func` datatypes get `_task-X_` injected from ProtocolName/SeriesDescription, defaulting to `"rest"` to match the C `%h` `task-rest_bold.json` boilerplate. Collisions get `_run-NN` (zero-padded, starting at `_run-01`, applied collectively across siblings; the un-numbered first arrival keeps its plain stem). Runs unconditionally before the rest of the post-pass; the dead `--bidsguess` flag from the previous iteration has been removed.

All-discard Unknown/ purge (`_purge_all_discard_unknown`): runs immediately after `_rescue_unknown_dir` on the same bids_root. If every remaining `*.json` in `Unknown/` has `BidsGuess[0] == "discard"` AND every non-JSON file has a matching JSON sidecar (no orphans), the whole `Unknown/` directory is `shutil.rmtree`'d. Discards are scout localizers, Phoenix Documents, and other non-image DICOMs the C side flagged as not-for-BIDS; leaving them in `Unknown/` and adding them to `.bidsignore` is the conservative fallback, but when EVERY file is a discard the folder itself is just noise. A single non-discard JSON (e.g. CT, which isn't in `_BIDS_DATATYPES`) preserves the folder so the `.bidsignore` sweep can route the file. Strictly an after-rescue cleanup: rescue moves out the rescuable files first, then this pass evaluates what's left. Audit H1 hardening (2026-06-05): the `iterdir()` loop refuses to purge when any child of `Unknown/` is a directory. dcm2niix itself never writes nested subdirectories there, but an external tool or a crashed prior run could; the previous code only inspected top-level files and would have `rmtree`'d an unrecognised nested tree blindly.

Path-traversal hardening on the rescue: `BidsGuess[0]` (datatype) is matched against `_BIDS_DATATYPES` (a fixed allowlist of BIDS top-level datatype dirs) and `BidsGuess[1]` (entity_suffix) is required to match `[A-Za-z0-9_-]*` before either reaches a `Path` component. `_session_token_from_studydatetime` validates that StudyDate is 8 digits (DICOM VR DA) and StudyTime starts with 6 digits (VR TM) before forming `YYYYMMDDTHHMMSS`. These checks are the consumption-side privacy/safety boundary — the C-side `reproinTsvField` strips only `\t\r\n`, so a malicious DICOM with e.g. `StudyDate="../../etc"` reaches the Python rescue unmodified and gets rejected here. Failing any check leaves the file in `Unknown/`; the `.bidsignore` sweep then routes it for the validator to skip. No data loss, no traversal.

Known small audit-deferred items on the rescue: (a) a mid-loop `OSError` during `src.rename(dst)` can leave the file family split across `Unknown/` and the new sub/ses dir — accepted because both are on the same filesystem and the only realistic failure path (EXDEV/EACCES on the parent) would already have failed `target_dir.mkdir`. (b) `_load_provenance` overwrites duplicate header columns silently (last-wins) — not user-reachable since only the C side writes the TSV. Both deferred until a regression motivates the work.

Intentional non-BIDS-datatype handling: `_BIDS_DATATYPES` covers the stable BIDS datatypes (`anat`, `func`, `dwi`, `fmap`, `perf`, `pet`, `mrs`, `meg`, `eeg`, `ieeg`, `beh`, `micr`, `nirs`, `motion`). `mrs` was added in the stable BIDS spec (1.11.1+) — see the BIDS-MRS section linked from `saveDcm2NiiMRS` — and matches the C side's `BidsGuess ["mrs","_svs"]` emission for SVS series. The C side at `nii_dicom_batch.cpp:~8250` can also emit `bidsDataType = "CT"`, but BIDS has no `ct` datatype; promoting CT files into `<root>/sub-X/ses-Y/ct/` would just trip the validator. Leaving them in `Unknown/` lets the `_bidsguess_unknown_leftover_files` sweep route them to `.bidsignore` — the user keeps the files, the validator stays clean. The CT exclusion is therefore intentional; do not add `ct` to `_BIDS_DATATYPES` without first confirming the validator accepts it (BEP-024 is not in the stable spec as of 2026).

Trust boundary for the collapse and rescue: both passes treat `out_root` as a single-user, trusted-input tree. TOCTOU races between `iterdir()` checks and `shutil.move` / `Path.rename`, and symlink-injection at chain dirs, are out of scope. Documented for any future audit that flags them: the rationale is that reproinx.py runs on the user's own conversion output immediately after dcm2niix finishes, with no concurrent writers expected and no untrusted process able to insert symlinks between the check and the move.

`_bidsguess_cleanup` (discard/ removal, 3D-bold→sbref demotion, single-volume DWI artifact patterns to `.bidsignore`, a/b/c collision suffixes to `.bidsignore`, residual `Unknown/` files to `.bidsignore`) also runs unconditionally — each sub-pass is a no-op when its target is absent, so the gate is harmless for clean `-f %H` trees while letting messy `-f %h` trees come out validator-clean. `_write_scans_tsv` consults `.bidsignore` so files marked there don't trip `SCANS_FILENAME_NOT_MATCH_DATASET`.

Physio rescue (`_rescue_physio_recordings`): dcm2niix `-f %H` routes Siemens physio payloads — the `(7FE1,1010)` blob in `RawDataStorage` SOPs, which can be either an XA-line gzip-XML PhysioLog document or a legacy CMRR Multi-Band binary PMU blob — under `derivatives/scanner/<sub>/[<ses>/]func/` with a filename like `sub-X_task-Y_acq-Z_run-N_bold_recording-LABEL_physio.{tsv.gz,json}`. The `_bold` (or `_sbref`/`_dwi`/...) infix is non-spec: per the BIDS physiological-recordings spec the physio filename inherits the parent modality's shared entities (sub/ses/task/acq/ce/rec/dir/run + `_recording-LABEL`) but NOT the modality suffix. The rescue runs after Pass 1 (session backfill) and before Pass 4 (`_drop_derivatives`): walks `derivatives/scanner/sub-X/**/func/` for `*_recording-*_physio.{tsv.gz,json}`, strips the infix via `_PHYSIO_INFIX_RE`, injects `_ses-Y` when the main tree pinned a single session for that subject, and moves to `<bids_root>/sub-X/[ses-Y/]func/`. Collisions (curated file already present) leave the source in place — non-destructive on hand-built trees.

C-side SBRef physio discard (`saveDcm2NiiCore` physio branch). On Siemens XA60 protocols that pair a legacy CMRR multiband SBRef with a Siemens product BOLD, both acquisitions emit their own physio series with overlapping BIDS stems (reproin parsing strips the SeriesDescription suffix, so `..._dicom_PhysioLog` and `..._dicom_SBRef_PMU` collapse to the same root). The SBRef physio is single-volume calibration data — useless for time-series physio modelling (RETROICOR etc.) and downstream tools (fmriprep) pair physio with the multi-volume BOLD, not the SBRef. The physio writer therefore discards any series whose SeriesDescription contains `_SBRef`, before reaching the format converters (xa/cmrrPhysioConvert). The remaining BOLD physio gets the natural BIDS name with entities that match the parent BOLD (entity-based pairing tools work correctly).

Subtle naming-vs-format quirk discovered while implementing this: the SeriesDescription suffix (`_PhysioLog` / `_SBRef_PMU`) reflects the **source acquisition's role**, not the **(7FE1,1010) payload binary format**. On the test XA60 dataset we observed:
- Series 7 (`..._dicom_PhysioLog`): payload starts `8a d3 02 00…` — CMRR PMU binary format (parser sets `isCMRRPhysio=true`).
- Series 8 (`..._dicom_SBRef_PMU`): payload starts `1f 8b 08 00…` — gzip magic, gzip-XML PhysioLog format (parser sets `isXAPhysio=true`).

So the discard gate must be on `SeriesDescription`, NOT on the `isXAPhysio` / `isCMRRPhysio` format flags. Likewise, any future per-format disambiguation work must not assume the description suffix predicts the format.

### SBRef BOLD detection regardless of protocol prefix

The Siemens BOLD branch in `setBidsSiemens` ([nii_dicom_batch.cpp:~8430](console/nii_dicom_batch.cpp#L8430)) detects Single-Band Reference (SBRef) volumes via `strstr(d->seriesDescription, "_SBRef")`. The previous gate also required `d->protocolName` to start with `"func_"` (old Siemens convention) which silently skipped ReproIn-style names like `"func-bold_task-rest_acq-dualecho_run-1_dicom"` — the SBRef of a typical SBRef+multiband BOLD pair was mislabelled as `bold` instead of `sbref`. Match the unconditional DWI-branch pattern: SeriesDescription contains `_SBRef` → `sbref`, regardless of protocol prefix. dcm2niix is single-pass; cross-series context is unavailable, and `_SBRef` is the strongest single-series clue (Siemens VE-line + XA-line both emit it for `ep2d_bold_SBRef`-class series). The TODO at issue753 about inferring task from `protocolName` is preserved as a standalone comment so the inference work stays tracked (commit 6ee8e94).

### Single-volume slice-timing escape in `checkSliceTiming`

`checkSliceTiming` validates per-slice timing falls within TR, but for single-volume anatomical scans (TSE/SPACE/MPRAGE/3D) TR is per-slice and slice times legitimately span many TRs; the previous "slice timing appears corrupted" warning was a false alarm. Early-clear the slice-timing array (silent — same treatment as the localizer/derived branch above) when `hdr->dim[4] < 2`. Issue870 multiband EPI detection is unaffected because EPI keeps within TR even at one volume (commit 210a4b0).

### PhaseEncodingDirection 3D multi-echo gate (issue 371 vs issue 849)

The outer gate at `nii_dicom_batch.cpp:~3283` used to be `if (!d.is3DAcq)` (issue849), which made the inner `echoTrainLength > 1` escape (issue371: ignore PE for 3D MP-RAGE/SPACE but report for 3D EPI) dead code. Replaced with `if (!isSkipPhaseEncodingAxis)` (commit fe90be1).

The escape gate itself was tightened in the 2026-06-06 external audit (H2). The naïve `(d.echoTrainLength > 1)` clause re-broke issue849 because Siemens 3D SPACE / 3D TSE / FLAIR-SPACE report `ScanningSequence = "SE\IR"` with ETL≈200+ — exactly the structural 3D class issue849 wanted to suppress, but with high ETL. Hardened to:

```c
if ((d.echoTrainLength > 1) && (strstr(d.scanningSequence, "SE") == NULL))
    isSkipPhaseEncodingAxis = false;
```

The `!SE` check is load-bearing — without it, 3D SPACE FLAIR (`dcm_qa_xb10/flair/`, ETL=214, `ScanningSequence "SE\IR"`) emits PhaseEncodingDirection and the issue849 regression returns. The check correctly admits 3D EPI BOLD (`EP`, no `SE`), Philips/GE multi-echo GRE QSM (`GR`, ETL>1; Philips reports number of echoes, e.g. ETL=5 on a Bipolar 7-echo QSM), and Siemens 3D multi-echo GRE QSM (`GR`-family). 3D MP-RAGE stays suppressed via the underlying ETL=1. The QSM consensus reference sidecars from 2022 (Siemens, Philips, GE) all carry `PhaseEncodingDirection` for their multi-echo GRE QSM — this gate matches that baseline.

### Issue870 multiband false-alarm suppression

When `checkSliceTiming` estimates a higher multiband factor than the DICOM CSA reports, the override IS correct (Siemens XA + CMRR multiband sequences leave the CSA tag stale at 1), so the CSA-write keeps happening — but the `printWarning` is a false alarm on those Siemens datasets. Suppress the warning when EITHER `d1->imageTypeText` contains `_MB_` (Siemens private per-frame ImageType marker, e.g. `ORIGINAL_PRIMARY_M_MB_DIS2D`) OR `d1->imageComments` contains `Unaliased MB` (CMRR text marker, e.g. `Not for diagnostic use, Unaliased MB4/PE4/LB`). Either marker alone is a deliberate vendor declaration that the acquisition is multiband; absent both, the warning still fires so true anomalies (e.g. corrupted slice timing on a non-MB scan) still surface (commit e9ff4c1).

Multi-session subjects are deferred in the Python rescue: it prints a warning and skips them because the source filename doesn't encode `_ses-` and disambiguating across sessions needs acquisition-time matching that the provenance TSV doesn't surface for raw-data series.

The reproinx post-pass reads `<studyRoot>/.reproin_provenance.tsv`, written by `reproinAppendProvenance` in `nii_dicom_batch.cpp` once per converted series. **Schema is gated on `-ba` mode**: default `-ba o`/`-ba n` writes 11 columns (`StudyInstanceUID, SeriesNumber, ProtocolName, SeriesDescription, StudyDescription, OutputStem, PatientAge, PatientSex, StudyDate, StudyTime, PatientID`); `-ba y` (full anon, via `reproinx.py --anonymize`) writes 6 columns and drops the five demographic/date/id fields so the hidden TSV cannot leak what the per-series JSON sidecar just stripped. The Python loader keys on header names so the mode change is transparent. The writer also detects a stale header on existing files (tab-count mismatch) and rotates to `.bak` before starting fresh — never silently mixes 6-col rows under an 11-col header, and the same peek catches pre-PatientID 10-col TSVs and rotates them too. Anything more identifying (PatientName, BirthDate, OperatorsName, AccessionNumber, ReferringPhysicianName) is deliberately withheld in every mode — the file is the privacy boundary between the C and Python sides. Only written when the filename format contains literal `%H`. PatientID lives at column 11 so the Unknown/-rescue post-pass can derive a heudiconv-style `sub-<id>` for series whose protocol did not parse as ReproIn; it is intentionally absent in `-ba y` so anonymised conversions cannot recover the subject identifier.

`-ba` has three modes: `y` (default; strip dates AND patient PII), `n` (keep both), and `o` (omit PII only — strip the patient block but keep `AcquisitionDateTime`/`AcquisitionTime`). The `o` mode is what `reproinx.py` invokes so its `_scans.tsv` aggregation and fmap closest-time tie-breaker still have timestamps. `isOmitPiiBIDS` and `isAnonymizeBIDS` are independent flags in `TDCMopts`; `o` sets `isAnonymizeBIDS=false, isOmitPiiBIDS=true`.

The C code in `createDummyBidsBoilerplate(char *pth, bool isFunc, const char *taskName, const char *acqName)` in `nii_dicom_batch.cpp` writes the task sidecar with the *actual* entity set of the bold file (`task-<X>_bold.json` or `task-<X>_acq-<Y>_bold.json`). Keep this in lock-step with `spec.task`/`spec.acq` semantics in `reproin.cpp` — the legacy `%h` path passes `NULL/NULL` and still falls back to the historical `task-rest_bold.json` hardcoding. The post-pass must stay non-destructive on existing curated metadata: `README.md` → `README` cleanup is gated on `_is_dcm2niix_readme_stub`; generated root `task-X_bold.json` stubs are removed only when `_acq-` variants for the same task exist, and `TaskName` is written into each per-series BOLD sidecar first so bare task runs remain valid without triggering BIDS v2 multiple-inheritance errors. Hand-written sidecars with extra metadata and READMEs must survive re-runs untouched.

### TDICOMdata `deID_CS` ownership contract (issue #877)

`TDICOMdata.deID_CS` is a heap pointer owned by the original dcmList[] entry that called `readDICOMx`. Every other holder of a TDICOMdata struct (by-value parameters, retained shallow copies in `MRIFSSTRUCT.tdicomData`, R-side `TDicomSeries.representativeData`, ...) gets a NON-OWNING shallow copy that must NOT free it.

Rules:
- After `readDICOMx`/`readDICOMv`/`readDICOM` populates a `TDICOMdata`, that struct OWNS its `deID_CS` until released via `free_TDICOMdata_deID_CS(&d)`.
- After assigning `MRIFSSTRUCT.tdicomData = dcmList[indx]` or `series.representativeData = dcmList[i]`, the retained struct MUST set `.deID_CS = NULL; .deID_CS_n = 0;` (currently done at `nii_dicom_batch.cpp:10325-10327, 11459-11461, 12485-12487, 12506-12508`). The original dcmList[] retains ownership; the retained shallow copy is non-owning.
- Direct `readDICOM()` callers (where the result is not stored in a dcmList that the cleanup loop sees) MUST `free_TDICOMdata_deID_CS(&d)` before the result goes out of scope. Sites: `dcm2niix_fswrapper.cpp:226-228, 256-258`, `nii_dicom_batch.cpp:12206, 12278`.
- `free_TDICOMdata_deID_CS` is idempotent (no-op on NULL), so double-call is safe.

By-value parameter copies in function signatures (`nii_SaveBIDSX(... TDICOMdata d ...)`, `nii_createFilename(... TDICOMdata dcm ...)`, `isSameSet(... TDICOMdata d1, TDICOMdata d2 ...)`) are short-lived and reference the owner's deID_CS pointer for read access only. They go out of scope without calling free_TDICOMdata_deID_CS — that's correct because the owner is still live.

The hot-path by-value copy convention is documented as a pre-existing design choice; switching to `const TDICOMdata *` is a separate refactor that doesn't affect correctness given this ownership contract.

### Audit findings (this session's `/audit` round)

Two-agent audit (Security & Bugs + Refactor) of commits `ca309ef..5ff6508` (MR-weighting helper, deident-stack fix, Philips ASL widening, AcquisitionContrast integration, AC fallback). No CRITICAL / HIGH / MEDIUM security findings; three Refactor wins applied:

- **AC parser → table-driven** (`nii_dicom.cpp:~5902`): 13 sequential `strcmp` cases replaced with a `static const struct { cs, weighting, setDiffusion }` table + single loop. Adding a new DICOM PS3.3 enumerated value is now a one-row addition. `DIFFUSION` keeps its `d.isDiffusion = true` side effect via the `setDiffusion` flag so the back-compat is auditable in the table.
- **`setBidsFromAcquisitionContrast` snprintf** (`nii_dicom_batch.cpp:~1463`): bounded `snprintf(suffix + used, kDICOMStrLarge - used, "_%s", modality)` replaces raw `strcpy` + pointer math + manual bounds arithmetic. Same semantics, fewer footguns.
- **Removed redundant `deID_CS` init in `readDICOMx`** (`nii_dicom.cpp:~4450`): `clear_dicom_data()` already sets `d.deID_CS_n = 0; d.deID_CS = NULL;` at the top of `readDICOMx`. The "legacy parity" comment was misleading after the heap-pointer refactor — the legacy loop touched `dti4D->deID_CS[i]` which no longer exists.

Validated: in-tree `dcm_qa` / `dcm_qa_nih` / `dcm_qa_uih` zero drift; spot-checks on Canon DTI (fallback target), Philips SOURCE-pCASL (per-vendor AC gate target), and Philips fcMRI with `AC=T2` (must stay `func/_bold`) all unchanged.

Deferred audit suggestions:
- **Optional `isAslHintAC(d)` inline helper** to name the three duplicate `d->acquisitionContrast == kMRWeightingPerfusion` checks in vendor classifiers. Mild benefit; the duplication is intentional (each vendor's ASL branch coexists with vendor-specific siblings). Leave until a fourth vendor needs the same gate.
- **`MRWeightingGuess` / `setBidsFromAcquisitionContrast` shared table extraction.** Both consume `acquisitionContrast` and overlap on T1/T2/PD/T2starw/FLAIR/STIR. Audit reviewer rejected: the two functions have genuinely different jobs (return-int-for-cascade vs. write-strings-wholesale) and the de-dup is six switch arms.
- **Lazy `calloc` OOM in deident parser** (`nii_dicom.cpp:~5965`): on calloc failure, the first DeidentificationMethodCodeSequence entry's `CodeValue` is silently lost. OOM in practice is unrecoverable; deferred. A `printWarning` would be polite.

Pre-existing items still open from earlier audits:

### Audit findings (2026-06-06 round 3 external review)

External `audit_temp.md` reviewed `audit_response.md` and the dirty worktree through `fe90be1`. Two HIGH, seven MEDIUM, three LOW. Verified each claim by direct code/empirical inspection (3D SPACE Ref in `dcm_qa_xb10/flair`, Philips QSM Ref in `gump/`, all vendor branches). Outcomes:

- **H2 — 3D PhaseEncodingDirection restoration too broad. FIXED.** Empirically validated: SPACE FLAIR has `ScanningSequence "SE\IR"`, ETL=214. The fe90be1 gate (`ETL > 1` only) would emit PE for it. Tightened to `(ETL > 1) && !strstr(scanningSequence, "SE")`. Documented above under "PhaseEncodingDirection 3D multi-echo gate".
- **M4 — Siemens SBRef physio discard was silent. FIXED.** Now prints `Skipping SBRef physio series ...` at `opts.isVerbose > 0` before the EXIT_SUCCESS so users see the discard.
- **M5 — `MRIFSSTRUCT.dicomfile` leaked. FIXED.** Heap-allocated at `nii_dicom_batch.cpp:~11631` via `malloc()` but not freed by `nii_clrMrifsStruct()` or `nii_clrMrifsStructVector()`. Added `free(item.dicomfile); item.dicomfile = NULL;` to both, plus `mrifsStruct.dicomfile = NULL;` (and the `dicomlst`/`nDcm` siblings) immediately after the two `mrifsStruct_vector.push_back(mrifsStruct)` sites — ownership transfers cleanly to the vector entry without double-free risk. Same shallow-copy pattern as `deID_CS`.
- **M7 — HHMMSS slice-timing `maxT` was being compared with `<` instead of `>`. FIXED.** `maxT` was initialized to `minT` and only updated when `< maxT`, so it stayed pinned at the minimum. Broke midnight-crossing detection at `nii_dicom_batch.cpp:~7857` for UIH scans (and any forced-HHMMSS path) — `(maxT - minT) > kNoonSec` became `0 > kNoonSec` = false. One-character fix.
- **L1 — AcquisitionContrast docs stale. PARTIAL.** This session previously updated the fallback policy section but left the per-vendor description language ambiguous. The reality: per-vendor `AC=PERFUSION` is OR-with-vendor-evidence (Siemens: 6-term OR chain on sequence-name heuristics; Philips: OR with `ImageType=PERFUSION`/`aslFlags`; GE: OR with `seqName="asl"`); the fallback `setBidsFromAcquisitionContrast` returns early on AC=PERFUSION so DSC/DCE lands in `Unknown/` (commit 5ff6508 + the AC=PERFUSION-removal-from-fallback in the same review window).
- **L2 — Stale `xaPhysioConvert` contract comment. FIXED.** Comment said "EXIT_FAILURE if nothing was emitted"; rewritten to match commit 87c93f2's empty-payload → EXIT_SUCCESS+warning behaviour.
- **L3 — Diffusion-fallback comment overstated. FIXED.** Comment claimed two evidence sources (`numDti` OR `dim[4]>1`) but the code only checks `numDti`. Rewritten so the comment matches the gate: `numDti` is the proxy for "we actually parsed b-values" across every vendor path.

Refuted / debate / defer:

- **H1 — AC=PERFUSION vendor branches "standalone". REFUTED.** Direct read of all three sites confirms OR-with-vendor-evidence:
  - `nii_dicom_batch.cpp:~8353` (Siemens): part of a 6-term OR with sequence-name heuristics (`_asl`, `fairest`, etc.).
  - `nii_dicom_batch.cpp:~8633` (Philips): OR with `ImageType=PERFUSION` and `aslFlags != NONE`.
  - `nii_dicom_batch.cpp:~8861` (GE): OR with `seqName="asl"`.
  No DSC/DCE misclassification surface. CLAUDE.md's "alongside vendor-specific ASL evidence" wording is accurate.
- **M1 — Physio rescue collision deletes source. DEBATED.** The framing is wrong. When the destination already contains a curated/manual physio file, rescue skips the move and the source stays in `derivatives/scanner/`. If `--keep-derivatives` is false the source is dropped — but the destination IS the kept curated copy. No data loss in the user's intent: the curated file wins, the auto-rescued duplicate is discarded. Documented intent; no fix.
- **M2 — Rescue moves `.tsv.gz` and `.json` independently. DEFERRED.** Orphan-pair on partial collision IS a real edge case (move one half of a pair, skip the other) but requires pair-grouping logic in the rescue loop. Low impact in practice — collisions are user-curated overrides and orphans get caught by `_bidsguess_cleanup`'s residual sweep. Track for next reproinx-focused pass.
- **M3 — `--keep-derivatives` not honoured for rescued physio. DEFERRED — semantic question.** The current behaviour treats `--keep-derivatives` as "retain the scanner derivatives tree that's left AFTER promotion". Honouring the flag strictly would require copy-not-move during rescue, which has its own footguns (which is the canonical copy if both exist after later edits). Will revisit when a user reports the actual confusion.
- **M6 — Direct `readDICOM()` early-return leaks. DEFERRED.** Reaffirmed pre-existing; same accept-with-rationale as previous audits. Unified-cleanup refactor is a focused project.
- **Carried-forward items** (MRS `kMRSAcqNone`, DeID `CodeSequenceMacro` tag-order dependency, `copyFile()`/`readDICOMx()` early-failure cleanup, hot-path `TDICOMdata` value copies). All previously accepted as deferred; no new evidence that any has crossed the threshold.

### Audit findings (2026-06-06 round 2 external review)

External `audit_temp.md` covered `793ad0e..HEAD` and flagged 3 HIGH, 6 MEDIUM, 2 LOW. Response in `audit_response.md`. Summary of fixes applied:

- **H1**: `nii_clrMrifsStructVector()` rewritten — fixed loop-variable shadow, wrong-vector-bound, and `new[]/free()` mismatch. Both clear functions now NULL pointers / reset counts / `mrifsStruct_vector.clear()`. FreeSurfer-wrapper-only path.
- **H2 + L2**: `TDICOMdata.deID_CS` ownership contract enforced — retained shallow copies in `MRIFSSTRUCT.tdicomData` and R-side `TDicomSeries.representativeData` get `.deID_CS = NULL; .deID_CS_n = 0;` immediately after the shallow copy. Direct `readDICOM()` callers (`dcm2niix_fswrapper.cpp`, R rename paths) now release the result before going out of scope. Full contract documented in the "TDICOMdata `deID_CS` ownership contract" section above.
- **H3**: AC=PERFUSION removed from the fallback `setBidsFromAcquisitionContrast` — `PERFUSION` is the DICOM enumeration for ASL AND DSC/DCE, so the bare-tag fallback would mis-label DSC/DCE files. Per-vendor AC=Perfusion gates remain because each coexists with vendor-specific ASL evidence (Philips `aslFlags`/`ImageType=PERFUSION`, Siemens/GE sequence-name asl/pcasl).
- **M1**: AC=DIFFUSION fallback now gated on `d->CSA.numDti >= 1` — bare AC=DIFFUSION without parsed gradients no longer produces a `_dwi` file with empty `.bval/.bvec`. Canon Enhanced MR DTI still classifies correctly because Canon's parser populates `CSA.numDti`.

Audit-deferred (response in audit_response.md):
- **M2 MRS `kMRSAcqNone` inconsistency** — pre-existing MRS edge case, separate concern.
- **M3 deident parser tag-order / CodeMeaning dependency** — real hardening concern but only triggers on malformed DICOMs (tags within a sequence item are spec-required to be tag-number-ordered) or items missing CodeMeaning. dcm_qa_deident's 5 entries verified byte-identical vs Ref. Fix requires item-delimiter `(FFFE,E00D)` tracking interaction with parser's SQ-depth.
- **M4 `copyFile` errors as success + handle leaks** — pre-existing, not from review window.
- **M5 `readDICOMx` early-exit resource leaks** — pre-existing, three early-return paths leak file/buffer/dcmDim. Unified-cleanup refactor is a focused project.
- **M6 by-value `TDICOMdata` in hot paths** — pre-existing convention. The ownership angle is now mitigated by the H2 fix; CPU/cache angle is a separate refactor.
- **L1 AC policy spread across 5 sites** — accepted as documented; the spread reflects the conceptual structure (parse / weighting consume / fallback consume / vendor gates consume).

### Audit-deferred items from 2026-06-06 follow-up review

- **MRS multi-nucleus / 2D-spectroscopy array fields.** BIDS-MRS allows `ResonantNucleus` and `SpectrometerFrequency` to be arrays for multi-nucleus or 2D-spectral acquisitions. Our DICOM parser collapses VM 1..2 fields to scalars, so multi-nucleus output would only carry the first frequency. Deferred until reference data surfaces.
- **MRS big-endian-host build.** `saveDcm2NiiMRS` byte-swaps when `!d.isLittleEndian`. On a big-endian build that would swap unnecessarily for native little-endian DICOM payloads. dcm2niix has no big-endian CI; accepted as not a real-world concern.
- **No in-repo MRS sidecar fixture test.** `dcm_qa_mrs` validates the byte-identical FID + sidecar round-trip but lives out-of-tree. The C-side BIDS-MRS field rename (`TransmitterFrequency` → `SpectrometerFrequency`, etc.) means the user's `dcm_qa_mrs/Ref/*.json` needs a refresh after each schema bump. A small in-repo synthetic fixture would catch silent drift; deferred until the field set stabilises.

### Audit-deferred items from 2026-06-05 follow-up review

The second 2026-06-05 audit (focused on Phase B MRS) raised four
architectural / coverage items that were not fixed in code. Persisting them
so the next audit doesn't re-litigate.

- **`saveDcm2NiiMRS` is a handwritten mini-pipeline** that duplicates allocation, file reading, endian handling, header construction, naming, and sidecar policy. Acceptable for the SVS-only Phase B scope but it will not scale cleanly to MRSI / Unloc / mrsref without extracting shared validation and write helpers. Refactor when Phase C lands.
- **No in-repo regression coverage for VIBE classifier / SWI override / all-discard Unknown purge.** MRS is covered by `dcm_qa_mrs`. The Siemens-specific classifications (VIBE physics, SWI ImageType override, SPACE/FLAIR detection) and the `_purge_all_discard_unknown` path need synthetic or fixture-based tests. Cite-in-CLAUDE.md is not a replacement for an executable test.
- **BJNIfTI complex split path has inconsistent header/data shapes** ([nii_dicom_batch.cpp:~5861-5862, ~5895-5897, ~6072-6078](console/nii_dicom_batch.cpp#L5861-L6078)). `_ArraySize_` increments `ndim` and appends a component dimension, but the header `Dim` is written from `hdr.dim + 1` with the incremented count. Complex64 MRS exported via `-e b` would have inconsistent shape — currently moot because the MRS path rejects non-NIfTI save formats at dispatch (audit L2 fix). Worth fixing when BJNIfTI grows complex support; defer until then.
- **The Siemens BIDS classifier cascade has accumulated special-case policy** (VIBE physics, SWI ImageType override, SPACE FLAIR, multiple seqDetails branches). Next Siemens rule should extract small predicate/classifier helpers rather than add another `else if`. Not done yet because the cost of churning the cascade is real and no individual addition has crossed the threshold.

### Audit-deferred accepted-risk decisions (2026-06-05 external review)

Several findings from the 2026-06-05 external audit were accepted-with-rationale rather than fixed. Persisting them here so future audits don't re-litigate:

- **DWI derivative override can flip vendor-marked "derived" to "dwi"** ([nii_dicom_batch.cpp:~1402-1406](console/nii_dicom_batch.cpp#L1402-L1406)). When the protocol text contains a word-boundary `_FA`/`_ADC`/`_colFA`/`_expADC`/`_trace`/`_S0map` token, the BIDS-Manager-derived heuristic overrides any prior `dataTypeBIDS = "derived"` set by `setBidsSiemens`/`setBidsGE`/`setBidsPhilips`. Intentional: BIDS treats FA/ADC/trace as raw `dwi/` outputs with their canonical suffixes, not as `derivatives/scanner/` files. The word-boundary check (`bidsFindTokenBdy`) blocks substring drift on names like `MPRAGE_FATsat`. The corresponding `_TENSOR` case routes to `"derived"` (TENSOR has no canonical raw suffix).
- **`kAutoAlignData` (`0021,103F`) localizer detection has broad downstream effect** ([nii_dicom.cpp:~7182-7193](console/nii_dicom.cpp#L7182-L7193)). Setting `isLocalizer` suppresses the issue-742 partial-volume warning and routes acquisitions to `discard` in vendor classifiers. Accepted because the detection is narrow: Siemens-only, gated on the private XA UT tag containing the literal substring `LOCALIZER`. A non-scout XA acquisition that legitimately carries `LOCALIZER` in this tag would be misclassified; mitigation is to add a regression sample if one ever surfaces.
- **`_bidsguess_cleanup` deletes `discard/` and `derivatives/scanner/` without ownership markers**. The cleanups operate on dcm2niix's own conversion output and the trust boundary (`out_root` is a single-user trusted tree) is documented elsewhere in this file. A user running `--no-convert` against a curated tree that already contains legitimate `discard/` or `derivatives/scanner/` would lose those subtrees. Mitigation: don't run reproinx.py against curated trees. A future `--no-cleanup` opt-out flag is the right next step if a user reports loss.
- **C and Python duplicate BIDS suffix policy**. The C side emits guessed suffixes; `_BIDSGUESS_COLLISION_RE` in `tools/reproinx.py` knows which suffixes are collision-prone. The 2026-06-05 audit caught the DWI-derivative suffixes (`_FA`, `_ADC`, etc.) missing from the regex; both sides were updated in tandem. Persistent coupling concern: any future C-side suffix addition (e.g. MRS variants `_mrsi`/`_unloc`/`_mrsref`) must mirror into the regex. The MRS commit already pre-populated those four in the regex.
- **Repeated tree walks in `_post_process`** (`_walk_subjects`, `_walk_sessions`, `_bids_roots`, `rglob`). Each pass re-scans; on large studies this is O(N) per pass × ~6 passes. Acceptable for current dataset sizes (< 100 series). Defer caching until a user reports actual slowness.

### MR Spectroscopy (MRS) pipeline (`saveDcm2NiiMRS`)

dcm2niix handles MR Spectroscopy Storage SOP DICOMs (UID `1.2.840.10008.5.1.4.1.1.4.2`) through a dedicated converter that lives at `nii_dicom_batch.cpp:~saveDcm2NiiMRS`. SOP class detection at `nii_dicom.cpp:~5577` sets `d.isMRS = true`; the `(5600,0020)` Spectroscopy Data handler at `nii_dicom.cpp:~7618` captures the FID payload offset (was previously a "Skipping Spectroscopy DICOM" rejection); the `isValid` gate at `nii_dicom.cpp:~8358` admits `1×1×1` MRS files; and `saveDcm2Nii` dispatches to `saveDcm2NiiMRS` at its top when the lead DICOM has `isMRS`.

Input: N DICOMs of one series, each carrying a single FID (interleaved real/imag float32) in `(5600,0020)`. Output: a complex-valued NIfTI-1 with `datatype=DT_COMPLEX64 (32)`, `dim=[5,1,1,1,DataPointColumns,N,1,1]` (4D when N=1), `pixdim=[1, vox_x, vox_y, vox_z, 1/SpectralWidth, 1, 1, 1]`, and `sform_code=2` with the affine computed via the spec2nii formula: `Q^T @ diag([PixelSpacing[1], PixelSpacing[0], SliceThickness])` then LPS→RAS negation of rows 0–1.

NumarisX phase convention: XA-line Siemens stores the FID as `real - 1j·imag`; the writer negates each odd-indexed (imag) float **except when it's already zero**, so `+0.0` is preserved (avoids byte differences from `-0.0` vs `+0.0` versus the spec2nii reference). VE/VX scanners use `real + 1j·imag` and the negation is skipped.

JSON sidecar (BIDS-MRS compliant, alongside the standard fields). Required: `ResonantNucleus`, `SpectrometerFrequency` (= `imagingFrequency`, MHz), `SpectralWidth`, `EchoTime`. Recommended: `NumberOfSpectralPoints` (= `dataPointColumns`), `AcquisitionVoxelSize` (`[xyzMM[1], xyzMM[2], zThick]` — `zThick` not `xyzMM[3]` because the latter takes on SpacingBetweenSlices when present), `NumberOfTransients` (prefer DICOM `NumberOfAverages` over `dim[5]` because the latter could equally count coils), and `ScanningSequence` constrained to the BIDS-MRS vocabulary `SVS` / `MRSI` / `Unlocalized MRS` (the general-path `ScanningSequence` emission at `nii_dicom_batch.cpp:~1889` is gated `!d.isMRS` so the MRS-side BIDS value is the sole writer). Also `DwellTime = 1/SpectralWidth` and a `printWarning` when any required field is missing. The existing Siemens-EPI `DwellTime` emitter at `nii_dicom_batch.cpp:~2960` is gated `!d.isMRS` so the MRS-derived DwellTime is the only one written.

BidsGuess: `["mrs","_svs"]` for SVS (currently the only path). MRSI / Unloc / mrsref variants will land when reference data is available.

Validated against `/Users/chris/src/spec2nii/XA60/Ref/series{30,31}.nii.gz` (64-DICOM and 1-DICOM SVS series): image-data payloads are **byte-identical**, sform values match to float32 precision (spec2nii uses NIfTI-2/float64). In-tree regression (`dcm_qa`, `dcm_qa_nih`, `dcm_qa_uih`) is clean — the MRS path is entered only when `isMRS` fires, which no regression sample triggers. MRS-specific regression coverage lives in the out-of-tree `dcm_qa_mrs` submodule (run by the next external review; the in-tree trio gate does not exercise it).

2026-06-05 Phase-B audit hardening on this pipeline:
- **Endian normalisation (H2):** each FID slot is byte-swapped via `nifti_swap_4bytes` when `d->isLittleEndian` is false. XA-line Siemens always emits Explicit VR Little Endian, but the swap covers Explicit VR Big Endian MRS Storage which is legal per the DICOM spec.
- **dim overflow guard (H4):** `DataPointColumns > 32767` or `N_files > 32767` (NIfTI-1 `dim[k]` limits) is rejected before allocation; `nii_ImgBytes(hdr) == total_bytes` is asserted after header construction so future internal accounting drift fails loud.
- **DT_COMPLEX64 byte-swap (H3):** `swapEndian()` now special-cases `DT_COMPLEX64` and swaps each float32 component (2×nVox of them) rather than treating `(real,imag)` as a single 8-byte scalar. `--big-endian y` on MRS output is now correct.
- **Stack invariant checks (M2):** every member of an MRS series must agree with `d0` on `isMRS`, `dataPointColumns`, `spectralWidth`, `isLittleEndian`, `manufacturer`, and `isXA`. Mismatch aborts before any FID is read.
- **Affine validity gate (M3):** zero / NaN / Inf in orient or position, or non-positive voxel spacing, sets `sform_code=0` with a `printWarning` instead of stamping an authoritative-but-bogus geometry.
- **Foreign save-format rejection (L2):** MGH / NRRD / BJNIfTI save formats are rejected at MRS dispatch with a direct diagnostic, so they don't read the FID just to fail at the writer (and bypasses the BJNIfTI complex-split inconsistency noted in the deferred items section).
- **ResonantNucleus JSON escaping (L1):** emission uses `json_Str` rather than raw `fprintf` so a malformed DICOM CS containing a quote or backslash gets escaped rather than breaking the sidecar JSON.

Attribution: ported from spec2nii (BSD-3-Clause, William Clarke, U. Oxford 2020; `spec2nii/Siemens/dicomfunctions.py` for the parse / phase logic, `spec2nii/dcm2niiOrientation/orientationFuncs.py` for the affine).

### Siemens SPACE / FLAIR detection in `setBidsSiemens`

Two related fixes for variable-flip-angle TSE FLAIR misclassification:
1. **The bold cascade's `pace` check is now anchored on `_pace`** ([nii_dicom_batch.cpp:~8079](console/nii_dicom_batch.cpp#L8079)). The pre-fix `strstr(seqDetails, "pace")` matched `"space"` as a substring (the comment "n.b. Space is not pace" was a warning, not enforcement). The fix matches `"_pace"` so the Siemens PACE (Prospective Acquisition Correction) sequences `ep2d_bold_PACE` / `ep2d_pace` still hit while `\space` does not.
2. **The `tse_vfl` branch now also fires on `\space` and consults `pulseSequenceName`** ([nii_dicom_batch.cpp:~7982](console/nii_dicom_batch.cpp#L7982)). Siemens SPACE (Sampling Perfection with Application-optimized Contrasts) is the same variable-flip-angle TSE family as `tse_vfl`, just the marketing name on XA-line scanners. The FLAIR override inside that branch now also checks `d->pulseSequenceName` for `"spcir"` because on XA-line Siemens `d->sequenceName` can be empty while the inversion-recovery signal lives in `pulseSequenceName` (e.g. `*spcir_220ns`).

Together: `/Users/chris/src/dcm_qa_xb10/flair/in/` with `seqDetails="%SiemensSeq%\\space"`, `pulseSequenceName="*spcir_220ns"` now correctly produces `["anat","_acq-spcir2p2_run-5_FLAIR"]` (was misclassified as `["func","_acq-spcir2p2_dir-RL_run-5_bold"]` via the `pace`-substring path).

### `MRWeightingGuess` shared classifier + DICOM AcquisitionContrast

`MRWeightingGuess(d, isSpinEcho, isVariableFlipAngle)` in [`nii_dicom_batch.cpp`](console/nii_dicom_batch.cpp) (prototype in [`nii_dicom_batch.h`](console/nii_dicom_batch.h); `kMRWeighting{Unknown,T1,T2,PD,T2starw,FLAIR,STIR,Diffusion,Perfusion,TOF,Flow,Tagging,Mixed,Other}` enum in [`nii_dicom.h`](console/nii_dicom.h)) is the single source of truth for MR weighting classification, shared by the four sites below.

**Source-of-truth order inside the helper:**

1. **DICOM (0008,9209) Acquisition Contrast** if populated to a weighting value (`T1` / `T2` / `PROTON_DENSITY` / `T2_STAR` / `FLUID_ATTENUATED` / `STIR`). Vendor-agnostic and authoritative — trust it over the physics estimate. The parser at `nii_dicom.cpp:~5902` maps the CS string to the enum and stores on `d.acquisitionContrast`. AC values like `DIFFUSION` / `PERFUSION` / `TOF` / `FLOW_ENCODED` / `TAGGING` / `MIXED` / `OTHER` / `UNKNOWN` are acquisition-class markers (routed at the dataType level by the BIDS classifiers, not here) — they fall through to physics so the legacy `T1`/`T2`/`PD`/`T2starw` return space is preserved for the four call sites.
2. **Bottomley + Ernst-angle physics:**
   - T1 estimated via Bottomley's approximation `0.8 * fieldStrength^0.38` (sec) — scales correctly across ultra-low-field (Hyperfine ~0.064 T) and ultra-high-field (7 T+).
   - T2* estimated as `0.050 / fieldStrength` (sec) — susceptibility scales inversely with B0.
   - True T2 changes minimally with B0 vs T2*, so the SE T2 arm uses a **fixed 45 ms TE threshold** rather than a B0-scaled one.
   - GRE thresholds match the historical fl3d_vibe classifier verbatim: `TE >= 0.5 * T2*_est` → T2starw; `flipAngle >= 1.3 * Ernst` → T1; `flipAngle <= 0.7 * Ernst` → PD; default PD.

SE arm only consumes TE; GRE/Ernst arm additionally requires positive TR, fieldStrength, flipAngle. `isVariableFlipAngle=true` forces `Unknown` on the physics fallback because SPACE / tse_vfl / FLAIR carry a nominal DICOM flipAngle that does NOT predict contrast — but the AC short-circuit fires first, so e.g. AC=FLUID_ATTENUATED still returns `kMRWeightingFLAIR` on a VFL-flagged SPACE-FLAIR series.

**Vendor-agnostic Acquisition Contrast routing for ASL.** The ASL detection branches in `setBidsSiemens`, `setBidsPhilips`, and `setBidsGE` also fire on `d->acquisitionContrast == kMRWeightingPerfusion` (alongside their vendor sequence-name / ImageType / private-tag checks). The DICOM standard tag is the cleanest vendor-agnostic signal when present; the existing per-vendor checks cover Classic MR DICOMs where the tag is absent.

The AC=DIFFUSION case sets `d.isDiffusion = true` alongside `d.acquisitionContrast = kMRWeightingDiffusion` — back-compat for non-AC-aware code paths that already gate on `isDiffusion`.

**Call sites (PD/T2 SE migrations changed the threshold from per-vendor 40/50 ms to the unified 45 ms):**
- Siemens `fl3d_vibe` (GRE): T1w / PDw / T2starw classifier. The marketing label is constant ("vibe") regardless of contrast; physics decides. Verified on `/Users/chris/src/dcm_qa_xb10/fx/10_t1_vibe_tra_cs22/` (3T, TR=5.18ms, TE=2.46ms, FA=20°): Ernst ≈ 5.3°, FA/Ernst = 3.8 → T1w.
- Siemens `tse2d` (SE): PDw / T2w. Previously `TE < 50` → PDw; now `TE >= 45` → T2w.
- Philips `SK+SE` (SE): PDw / T2w. Previously `TE < 40` → PDw; now `TE >= 45` → T2w.
- GE `FSE` (SE): PDw / T2w. Previously `TE < 40` → PDw; now `TE >= 45` → T2w.

Validated against the full `dcm_validate` vendor matrix (~40 submodules): byte-identical JSON sidecar / NIfTI / TSV output vs the pre-refactor build on every suite. No `_PDw` / `_T2w` Ref had TE in the [40, 50) ms drift window — Siemens TSE Refs have 11/92 ms, Philips SK+SE Refs have 9/12 ms — so the threshold change is empirically harmless against the test corpus.

Compile-time opt-out: there is none — the helper is too small and the call sites all dispatch through `if (w == kMRWeighting*)` checks that degrade to legacy `PDw` behavior on `kMRWeightingUnknown`. Future call sites (Hyperfine, 7T+, other vendor sequences) should prefer this helper over fresh hardcoded TE thresholds.

**Acquisition Contrast fallback (`setBidsFromAcquisitionContrast`).** Runs at the very end of `setBids()`, AFTER vendor cascade + `setBidsHeuristics`, gated on `d->CSA.bidsDataType[0] == '\0'`. Maps the DICOM-standard `acquisitionContrast` value to BIDS dataType + modality (T1 → anat/`_T1w`, T2 → anat/`_T2w`, PROTON_DENSITY → anat/`_PDw`, T2_STAR → anat/`_T2starw`, FLUID_ATTENUATED → anat/`_FLAIR`, STIR → anat/`_T2w`, DIFFUSION → dwi/`_dwi`, PERFUSION → perf/`_asl`, TOF → anat/`_angio`). Modality appended via `strcat(suffix, "_<mod>")` so any `_acq-`/`_dir-`/`_run-` entities that `setBidsHeuristics` already added are preserved (Canon DTI files come out as `_dir-PA_dwi`, not bare `_dwi`).

The fallback is **strictly additive** — it never overrides a non-empty `bidsDataType`. Verified across the full `dcm_validate` matrix: 23 newly classified files (all Canon enhanced DTI series, previously unclassified because there is no `setBidsCanon`), zero regressions on any Siemens / Philips / GE / UIH file that the vendor cascade already classifies. fMRI series with `AC=T2`, MP2RAGE with `AC=T1`, scanner-derived FA/ADC maps with `AC=DIFFUSION` all keep their vendor-cascade classifications because their `bidsDataType` is non-empty by the time the fallback runs. The per-vendor `AC == kMRWeightingPerfusion` gates from the prior change are KEPT — they fix vendor-cascade *misclassifications* (e.g. Philips SOURCE-pCASL routed to PDw), which the fallback can't catch because misclassified files have non-empty dataType.

`STIR → _T2w` is a workaround — BIDS has no canonical STIR suffix yet. Revisit when a BEP adds one.

### SWI ImageType override in `setBidsSiemens`

After the sequence-name cascade in `setBidsSiemens` (just before the `_acq-` entity construction in `nii_dicom_batch.cpp`), an `ImageType` token check fires: any series whose ImageType array contains `SWI` is classified as `anat/_T2starw` regardless of which sequence-name branch above matched, and `isDerived` is cleared so the trailing `if (isDerived) dataTypeBIDS = "derived"` clobber at the end of the function does not flip it back. Covers EPI-based SWI sequences (e.g. `*swi3d_epr` on Siemens XA80) that do not match the existing `fl3d` / `gre` / `ep_seg_fid` patterns, plus MINIMUM / SWI_Images derived projections. Test: `strstr(d->imageType, "_SWI")` — the underscore prefix ensures `SWI` matches as a token (DICOM ImageType is underscore-joined) and not as a substring of `SWIRL` etc. The pre-existing `ep_seg_fid` mIP / SWI_Images demote at line ~8031 is now reachable only when the SWI override doesn't fire (i.e. ImageType lacks `_SWI` but seriesDescription contains those tokens) — a narrow secondary path retained for back-compat.

### Philips ASL detection via `aslFlags` in `setBidsPhilips`

The ASL gate at the head of the `setBidsPhilips` cascade now fires on `(strstr(d->imageType, "PERFUSION") != NULL) || (d->aslFlags != kASL_FLAG_NONE)`. The `aslFlags` field is populated by the Philips private tag `(2005,1429) MRImageLabelType` (CS `LABEL` / `CONTROL`) parsed at `nii_dicom.cpp:~7600`, gated on `manufacturer == PHILIPS`; only the first character (`L` or `C`) is checked, so the value space is small and ASL-specific.

Why both signals are needed: "SOURCE -" raw label/control series (e.g. `SOURCE - pCASL`, `SOURCE - 3D_pCASL_6mm`, `WIP SOURCE - 3DpCASL`) strip the `PERFUSION` token from per-frame ImageType — Classic Philips emits `M_FFE\M\FFE` or `M_SE\M\SE`, Enhanced Philips emits per-frame `M\SE\M\SE` instead of the shared `PERFUSION\NONE`. The ImageType check alone misclassified them as `func/_bold` (SK+GR fall-through) or `anat/_PDw` (SK+SE fall-through). The private-tag check is the positive identifier and is safe to widen the gate with because Philips only writes `LABEL`/`CONTROL` to that tag on ASL acquisitions.

Validated against the four affected `dcm_validate` references:
- `dcm_qa_philips_asl/202_SOURCE_-_pCASL_classic` — was `func/_bold` → now `perf/_asl`
- `dcm_qa_philips_asl/302_SOURCE_-_ASL_MultiPhase_classic` — was `func/_bold` → now `perf/_asl`
- `dcm_qa_philips_asl/402_SOURCE_-_3D_pCASL_6mm_classic` — was `anat/_PDw` → now `perf/_asl`
- `dcm_qa_philips_enh/1903_WIP_SOURCE_-_3DpCASL_cl` — was `anat/_PDw` → now `perf/_asl`

The corresponding Ref/ files in those submodules need a refresh after this change lands; the new outputs are the correct ones.

### BIDS-Manager-derived heuristics (`setBidsHeuristics`)

`setBidsHeuristics(d)` in `nii_dicom_batch.cpp` runs at the end of `setBids` as a vendor-agnostic refinement pass over `bidsDataType` / `bidsEntitySuffix` / `bidsTask`. Ported from BIDS-Manager (`bidsmgr/classifier/sequence_dict.py`, MIT). Gated on `d->modality == kMODALITY_MR` and skipped when `bidsDataType` already contains `"discard"`. Source for all matching is the lowercased `ProtocolName + " " + SeriesDescription`. Four sub-passes, in order:

1. **DWI scanner-derivative override.** Word-boundary token match on `colfa` / `col_fa` / `col-fa` (→ `colFA`), `expadc`/`exp_adc`/`exp-adc` (→ `expADC`), `tracew` (before `trace`; → `trace`), `tensor` (→ `TENSOR` with `dataType="derived"` so the file lands under `derivatives/scanner/` via the existing return-value gate), `s0map`/`s0_map`/`s0-map` (→ `S0map`), `fa` (→ `FA`), `adc` (→ `ADC`). Overrides whatever the vendor heuristic chose because vendor code typically misclassifies scanner-derived diffusion maps as raw `_dwi`. Boundary check (`bidsFindTokenBdy`) is the load-bearing safety: `MPRAGE_FATsat` must NOT match `fa`, `dwi_AP` must NOT match `adc` via substring drift.
2. **Task-name hints fallback** — only fires when `bidsDataType == "func"` AND `bidsTask` is empty AND `_task-` is not already in the suffix. First tries explicit `task-<label>` at word boundary; else walks a 12-entry curated dict (`rest`, `movie`, `nback`, `flanker`, `stroop`, `motor`, `checkerboard`, `exec`, `paradigm`, `sparse`, `activation`, `task`) with simple substring match, returning the canonical label. Without this fallback, the `%h` filename path defaults to `task-rest` via the legacy hardcoded fallback at `nii_dicom_batch.cpp:~4267`; the hint dict gives a more honest task name for non-ReproIn protocols whose name encodes the task.
3. **`_acq-X` token extraction** — only fires when `_acq-` is not already in the suffix. Finds every word-boundary `acq-<alphanumeric>` and returns the longest match (BIDS-Manager's `extract_acq_token` semantics). Spliced into the suffix at canonical BIDS-2 entity order via `bidsInsertEntity`.
4. **`_dir-AP/PA/LR/RL` extraction** — only fires when `_dir-` is not already in the suffix. Word-boundary match (`bidsFindTokenBdy`) on `ap`/`pa`/`lr`/`rl` to avoid `spatial` → `pa`, `LR2024` → `lr` style false-positives. Earliest occurrence wins, uppercased.

`bidsInsertEntity` splices a new entity into canonical BIDS-2 order (`_task-`, `_acq-`, `_ce-`, `_rec-`, `_dir-`, `_run-`, `_echo-`, `_flip-`, `_inv-`, `_part-`, then the BIDS suffix word). It's a no-op when the entity is already present, so the four sub-passes can run independently. Helper `bidsFindTokenBdy(haystack, token)` mirrors the BIDS-Manager `(?:^|[_-])TOKEN(?=$|[_-])` regex idiom; `bidsIsBoundary(c)` treats NUL, `_`, `-`, space, `/`, `\`, `.`, `,` as boundary characters.

The pass touches ONLY the BIDS-guess fields and `bidsTask`; vendor-specific code at `setBidsSiemens`/`setBidsPhilips`/`setBidsGE` is unchanged. In-tree regression suite passes with zero new `BidsGuess` deltas — the heuristic only fires to fill gaps, never to rewrite vendor decisions. Attribution: BIDS-Manager (MIT, Copyright 2023 BIDS Manager).

### JPEG Lossless multi-fragment gate (issue #1013)

`nii_loadImgXL()` in `nii_dicom.cpp` short-circuits to `nii_loadImgXLCore()` when `dcm.compressionScheme == kCompressC3` and the offset table has >1 item. `kCompressC3` (JPEG Lossless 1.2.840.10008.1.2.4.7x) has its own working multi-fragment decoder in `nii_loadImgJPEGC3` -> `decode_JPEG_SOF_0XC3_stack` that walks the file directly for SOI markers; the generic per-frame `dti4D->offsetTable[]` loop above the gate cannot be trusted for C3 because the `dti4D` reaching the decode site is not always the one the parser filled (e.g. `saveDcm2Nii` copies `*dti4Ds = *dti4D` from a stage-1 dti4D). Do **not** "clean up" this gate or merge the C3 path back into the generic loop — it will reintroduce the regression.

### Multi-fragment single-frame encapsulation (issue #1017)

A single compressed frame can be split across multiple `(FFFE,E000)` Items inside encapsulated DICOM pixel data. The reassembly is codec-agnostic but each codec's loader site is its own wrapper.

Shared helper: `console/dicom_fragments.{h,cpp}` exposes `reassembleEncapsulatedFragments(fn, firstFragmentDataOffset, *outLen)` which walks the file in two passes (count + bound-check, then copy), returning a malloc'd heap buffer with the concatenated codec bitstream. **Returns NULL on single-fragment OR on error** — the codec wrapper treats NULL as "decode the original file from imageStart" (i.e. fall back to the pre-1017 path). Lengths are overflow- and file-length-bounded to reject malformed declarations before allocation.

Per-codec wrappers (5-15 lines each):
- `kCompressC3` (JPEG Lossless): `nii_loadImgJPEGC3` calls the helper, then `decode_JPEG_SOF_0XC3_mem(buf, len, ...)`. The decoder was split into a static `_core` plus a file wrapper and a buffer wrapper (`_mem`); see `jpg_0XC3.cpp`.
- `kCompressJP2K` (JPEG2000): `nii_loadImgCoreOpenJPEG` substitutes the reassembled buffer for the file-read block; OpenJPEG was already buffer-driven via `opj_stream_create_buffer_stream`.

Parser gate at `nii_dicom.cpp:~8190` ONLY admits multi-fragment when `numberOfFrames <= 1`. Multi-frame with multiple fragments per frame still errors out — the helper concatenates ALL following fragments, so without real frame-to-fragment boundary parsing it would mix frames. Do **not** widen this gate without persisting per-frame fragment ranges in `dti4D`.

Transfer-syntax classification rename (kCompressYes -> kCompressJP2K): the old name was confusingly used for both "JPEG2000 transfer syntax" and "decompression-enabled flag". Now `kCompressJP2K` is strictly the compressionScheme tag, and `compressFlag` (set in `nii_dicom_batch.cpp:~12024`) is the runtime decode-enabled toggle. A historical regression of this round (audit_temp.md 2026-06-03 H1) was that the global rename caught the `1.2.840.10008.1.2.5` (DICOM RLE Lossless) branch by accident — it must stay `kCompressRLE`. Double-check transfer-syntax-to-scheme assignments in `nii_dicom.cpp:~5615-5640` if you ever rename a compression constant again.

Known pre-existing items the external review surfaced but were not introduced this round: (a) `hdr2D` is heap-allocated in the per-frame loop at `nii_dicom.cpp:~3994-4020` and not freed on every exit path — small per-series leak in multi-frame encapsulated decode. (b) JPEG2000 `.91` (lossy) is gated on `compressFlag != kCompressNone` while `.90`/`.201`/`.203` are not, so build/runtime classification is asymmetric. (c) `nii_loadImgCoreOpenJPEG` has a couple of unchecked codec/stream allocations and `fopen` results. Leave for a separate codec-hygiene pass.

**Source-list fanout warning.** dcm2niix has FIVE source-list surfaces that must all agree: `console/CMakeLists.txt` (3 blocks), `console/makefile`, `console/windows.bat`, `console/notarize.sh`, and `COMPILE.md`. When `dicom_fragments.cpp` was added for issue #1017 it was wired into CMake + makefile only; the audit in 2026-06-03 (round 7 H1) caught the omission in windows.bat / notarize.sh / COMPILE.md before release. Any new `.cpp` added to `nii_dicom.cpp`'s call graph needs the same five-place update. The refactor path is to make CMake/makefile authoritative and have the manual scripts delegate; out of scope for now.

### AcquisitionTime / AcquisitionDateTime zero-pad (BIDS validator)

The JSON sidecar writer in `nii_dicom_batch.cpp` formats the seconds field with `%09.6f` (e.g. `06.647500`), **not** `%02.6f`. `%02` only sets a minimum total width (always exceeded), leaving `6.647500` — which breaks ISO 8601 readers and trips BIDS validator's `ACQTIME_FMT` rule. This is a regression-prone area; preserve the `%09.6f` formatter on both `AcquisitionTime` and `AcquisitionDateTime`.

### SequenceName fallback for XA60 fMRI

XA60 (and other Siemens XA-line) fMRI populates `(0018,9005) PulseSequenceName` but leaves `(0018,0024) SequenceName` empty. The JSON sidecar writer promotes `pulseSequenceName` into the `SequenceName` slot when `d.sequenceName` is empty so the BIDS validator's recommended `SequenceName` field is satisfied. The principle: when a tag the validator expects is empty on a known scanner, prefer a documented fallback over emitting an empty string — but only when the tag is actually empty, so VE-line acquisitions that populate both keep their distinct values.

### MatrixCoilMode "None" fallback

`nii_SaveBIDSX` in `nii_dicom_batch.cpp` always emits `MatrixCoilMode` on the Siemens CSA path. `csaAscii.patMode == 1` → `"SENSE"`, `== 2` → `"GRAPPA"`, anything else (e.g. `32` = pure SMS, `256` = CompressedSense, `-1` = not found) → `"None"`. If the CSA acceleration block is unreadable, the outer `else` branch also writes `"None"`.

Why: pure-SMS XA60 acquisitions (MB>1 with no in-plane iPAT) previously omitted `MatrixCoilMode` entirely and tripped the BIDS validator recommendation. `"None"` is honest — SMS does not perform in-plane channel reduction — and the change introduced zero diffs in the in-tree Siemens Ref/ set.

Known wart: for `patMode == 256` (CompressedSense) the same series will emit both `MatrixCoilMode: "None"` and `CompressedSensingFactor: N`. This is not strictly contradictory — BIDS `MatrixCoilMode` is analog channel combination, `CompressedSensingFactor` is k-space undersampling/reconstruction — but the pair looks odd. No in-tree CS reference data exercises this path; `dcm_qa_cs_dl` lives outside the in-tree gate. Do not "fix" this by suppressing the `"None"` for patMode==256 without first deciding whether CompressedSense scans should advertise a distinct MatrixCoilMode value.

Deliberate trade-off, flagged by external review (audit_temp.md 2026-05-20): emitting `"None"` for the catch-all (`patMode` ∉ {1,2} or CSA unreadable) conflates "absence of analog matrix coil mode" with "unknown / not parsed". Honest absence (omit field) would be strictly more accurate; we chose to emit `"None"` because the BIDS validator's recommendation nag is what most users actually feel, and the catch-all is reached only when iPAT is not in use. Revisit only with a positive vendor source for the analog channel-combine mode (e.g. `sCoilSelectMeas.aRxCoilSelectData[0].ucMode`) — not by reverting to silent omission.

Do not "tidy" the `(patMode != 1) && (patMode != 2)` arm into a single ternary or drop the outer `else`; both are load-bearing for validator compliance.

### PulseSequenceType heuristic

`nii_SaveBIDSX` in `nii_dicom_batch.cpp` emits the BIDS-recommended `PulseSequenceType` derived from `d.scanningSequence`, `d.sequenceVariant`, and `d.CSA.multiBandFactor`. Mapping (first match wins):

- `EP` + MB + `SE` → `"Multiband Spin Echo EPI"`
- `EP` + MB → `"Multiband Gradient Echo EPI"`
- `EP` + `SE` → `"Spin Echo EPI"`
- `EP` → `"Gradient Echo EPI"`
- `GR` + `MP` + `IR` → `"MPRAGE"`
- `GR` + `\SP` → `"Spoiled Gradient Echo"`
- `GR` → `"Gradient Echo"`
- `SE` + `IR` → `"Inversion Recovery Spin Echo"`
- `SE` → `"Spin Echo"`
- otherwise → field omitted (do not write `"Unknown"` or an empty string)

Why: the BIDS spec's own examples mix vendor marketing labels (`"SPGR"`, `"MPRAGE"`) with acquisition-class names (`"Gradient Echo EPI"`); we prefer the class name and only emit a marketing label (`"MPRAGE"`) where the `ScanningSequence`/`SequenceVariant` combination is unambiguous.

The `isSP` test checks all three legal token positions (leading `"SP\\"`, sole `"SP"`, mid-or-trailing `"\\SP"`) because the bare substring `"SP"` would false-positive on `OSP` (oversampling phase) and a single anchored check would miss `SP` when it is the leading token. The matching `SpoilingState` fallback at `nii_dicom_batch.cpp:2083` now uses the same three-clause check, so the two sites no longer disagree on leading-token `SP`. `isMP` does not need anchoring — no other Siemens variant code (SK/MTC/OSP/SP/SS/TRSS/NONE) contains `MP`. A future cleanup may extract a small token-aware helper unifying this with the BidsGuess block's inline `strstr` idioms; the project's CLAUDE.md "do no harm" guidance discourages introducing it for just two sites today.

Limitations to keep in mind: `d.CSA.multiBandFactor` is reliably populated only on Siemens and GE; UIH multiband EPI will be labeled `"Gradient Echo EPI"` without the multiband qualifier. Non-Siemens MPRAGE-equivalents (e.g. UIH `t1_gre_fsp_3d`) will fall through to `"Gradient Echo"` because the MPRAGE detector requires the Siemens-style `MP` variant flag. The `"MPRAGE"` label is also an **overgeneralization within Siemens**: MP2RAGE, PSIR, and vendor-specific magnetisation-prepared GRE all satisfy the same `GR\IR + MP` flag pair. If a future audit asks for more precision, tighten by gating on `d.sequenceName` / `d.pulseSequenceName` matching a `tfl*` allowlist rather than dropping the label outright — the user has explicitly approved `"MPRAGE"` as the default name for this pattern.

Compile-time opt-out: `#define myDisablePulseSequenceType` suppresses the entire block, matching the file's existing `myXxx` opt-out convention.

### ParallelReductionFactor* "1.0 is informative" emit gate

`nii_SaveBIDSX` in `nii_dicom_batch.cpp` (~line 2589) emits both `ParallelReductionFactorInPlane` and `ParallelReductionFactorOutOfPlane` whenever `d.accelFactPE`/`d.accelFactOOP` is `>= 1.0` — i.e. any time a real source populated the field, including a value of exactly `1.0` (no reduction in that axis). The previous gate was `> 1.0`, which silently dropped the field for un-accelerated axes even when the DICOM standard tags `(0018,9069)` / `(0018,9155)` were explicitly present. The BIDS validator emits `JSON_KEY_RECOMMENDED` for the missing-but-informative case, so the wider gate is the honest fix.

Default sentinel for both fields is `0.0` (set by `clear_dicom_data`), so the wider gate does not introduce spurious emissions on non-MR or non-parsed paths. Sources that legitimately set the value to exactly `1.0` include: the DICOM standard tags `(0018,9069)` / `(0018,9155)` themselves, GE ASSET R-factor reciprocals, Siemens CSA `sPat.lAccelFactPE` / `sPat.lAccelFact3D` (which the scanner always populates), and Philips PAR/REC PhaseSlice values. Reviewer wanted source-presence boolean flags; we deferred — the `>= 1.0` gate is sufficient signal in practice, and adding flags would require touching every setter site.

### dataset_description.json BIDS validator compatibility (reproinx.py)

`tools/reproinx.py:_upgrade_dataset_description` writes the heudiconv template extended with three keys the BIDS validator recommends but heudiconv's stock template omits: `DatasetType: "raw"`, `GeneratedBy` (single dcm2niix entry with `ConversionSoftwareVersion` from a per-series sidecar), and `SourceDatasets: []`.

Why each is load-bearing:
- `DatasetType: "raw"` is **mandatory** when `GeneratedBy` is present. The validator's `src/schema/context.ts:80-84` infers `DatasetType = "derivative"` whenever `GeneratedBy` exists and `DatasetType` is absent, contradicting the schema's documented "default raw" behaviour. Without the explicit `"raw"`, dozens of derivative-mode rules fire (e.g. `SkullStripped` required on every anat `.nii.gz` — observed cascade: 36 spurious errors). This is the single most important key in the upgrade.
- `GeneratedBy.Version` is discovered lazily via `_discover_dcm2niix_version`, which sorts the candidate list (deterministic for mixed-version reruns), skips `derivatives/`, and prefers sidecars whose `ConversionSoftware` is literally `"dcm2niix"`. When no sidecar carries the field (e.g. `--no-convert` on an empty tree), the `Version` key is omitted rather than written as `"unknown"`.
- `SourceDatasets: []` silences the validator's recommendation; the empty array is semantically "explicitly no sources known," which is honest for DICOM-only inputs. Reviewer flagged this as "weaker than omission"; the user has approved keeping the empty array to satisfy the validator.

Hand-edited `dataset_description.json` (Name not the dcm2niix placeholder) is preserved, but the three recommended keys are *backfilled* when missing — never overwriting any other curated keys. This is a deliberate divergence from heudiconv's exact-bytes reproin scaffolding (the 7-file byte-identity property now holds for 6/7; `dataset_description.json` diverges by design).

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
