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

All-discard Unknown/ purge (`_purge_all_discard_unknown`): runs immediately after `_rescue_unknown_dir` on the same bids_root. If every remaining `*.json` in `Unknown/` has `BidsGuess[0] == "discard"` AND every non-JSON file has a matching JSON sidecar (no orphans), the whole `Unknown/` directory is `shutil.rmtree`'d. Discards are scout localizers, Phoenix Documents, and other non-image DICOMs the C side flagged as not-for-BIDS; leaving them in `Unknown/` and adding them to `.bidsignore` is the conservative fallback, but when EVERY file is a discard the folder itself is just noise. A single non-discard JSON (e.g. CT, which isn't in `_BIDS_DATATYPES`) preserves the folder so the `.bidsignore` sweep can route the file. Strictly an after-rescue cleanup: rescue moves out the rescuable files first, then this pass evaluates what's left.

Path-traversal hardening on the rescue: `BidsGuess[0]` (datatype) is matched against `_BIDS_DATATYPES` (a fixed allowlist of BIDS top-level datatype dirs) and `BidsGuess[1]` (entity_suffix) is required to match `[A-Za-z0-9_-]*` before either reaches a `Path` component. `_session_token_from_studydatetime` validates that StudyDate is 8 digits (DICOM VR DA) and StudyTime starts with 6 digits (VR TM) before forming `YYYYMMDDTHHMMSS`. These checks are the consumption-side privacy/safety boundary — the C-side `reproinTsvField` strips only `\t\r\n`, so a malicious DICOM with e.g. `StudyDate="../../etc"` reaches the Python rescue unmodified and gets rejected here. Failing any check leaves the file in `Unknown/`; the `.bidsignore` sweep then routes it for the validator to skip. No data loss, no traversal.

Known small audit-deferred items on the rescue: (a) a mid-loop `OSError` during `src.rename(dst)` can leave the file family split across `Unknown/` and the new sub/ses dir — accepted because both are on the same filesystem and the only realistic failure path (EXDEV/EACCES on the parent) would already have failed `target_dir.mkdir`. (b) `_load_provenance` overwrites duplicate header columns silently (last-wins) — not user-reachable since only the C side writes the TSV. Both deferred until a regression motivates the work.

Intentional non-BIDS-datatype handling: `_BIDS_DATATYPES` is the BIDS spec's set (`anat`, `func`, `dwi`, `fmap`, `perf`, `pet`, `meg`, `eeg`, `ieeg`, `beh`, `micr`, `nirs`, `motion`). The C side at `nii_dicom_batch.cpp:~8250` can also emit `bidsDataType = "CT"`, but BIDS has no `ct` datatype; promoting CT files into `<root>/sub-X/ses-Y/ct/` would just trip the validator. Leaving them in `Unknown/` lets the `_bidsguess_unknown_leftover_files` sweep route them to `.bidsignore` — the user keeps the files, the validator stays clean. The CT exclusion is therefore intentional; do not add `ct` to `_BIDS_DATATYPES` without first confirming the validator accepts it (BEP-024 is not in the stable spec as of 2026).

Trust boundary for the collapse and rescue: both passes treat `out_root` as a single-user, trusted-input tree. TOCTOU races between `iterdir()` checks and `shutil.move` / `Path.rename`, and symlink-injection at chain dirs, are out of scope. Documented for any future audit that flags them: the rationale is that reproinx.py runs on the user's own conversion output immediately after dcm2niix finishes, with no concurrent writers expected and no untrusted process able to insert symlinks between the check and the move.

`_bidsguess_cleanup` (discard/ removal, 3D-bold→sbref demotion, single-volume DWI artifact patterns to `.bidsignore`, a/b/c collision suffixes to `.bidsignore`, residual `Unknown/` files to `.bidsignore`) also runs unconditionally — each sub-pass is a no-op when its target is absent, so the gate is harmless for clean `-f %H` trees while letting messy `-f %h` trees come out validator-clean. `_write_scans_tsv` consults `.bidsignore` so files marked there don't trip `SCANS_FILENAME_NOT_MATCH_DATASET`.

The reproinx post-pass reads `<studyRoot>/.reproin_provenance.tsv`, written by `reproinAppendProvenance` in `nii_dicom_batch.cpp` once per converted series. **Schema is gated on `-ba` mode**: default `-ba o`/`-ba n` writes 11 columns (`StudyInstanceUID, SeriesNumber, ProtocolName, SeriesDescription, StudyDescription, OutputStem, PatientAge, PatientSex, StudyDate, StudyTime, PatientID`); `-ba y` (full anon, via `reproinx.py --anonymize`) writes 6 columns and drops the five demographic/date/id fields so the hidden TSV cannot leak what the per-series JSON sidecar just stripped. The Python loader keys on header names so the mode change is transparent. The writer also detects a stale header on existing files (tab-count mismatch) and rotates to `.bak` before starting fresh — never silently mixes 6-col rows under an 11-col header, and the same peek catches pre-PatientID 10-col TSVs and rotates them too. Anything more identifying (PatientName, BirthDate, OperatorsName, AccessionNumber, ReferringPhysicianName) is deliberately withheld in every mode — the file is the privacy boundary between the C and Python sides. Only written when the filename format contains literal `%H`. PatientID lives at column 11 so the Unknown/-rescue post-pass can derive a heudiconv-style `sub-<id>` for series whose protocol did not parse as ReproIn; it is intentionally absent in `-ba y` so anonymised conversions cannot recover the subject identifier.

`-ba` has three modes: `y` (default; strip dates AND patient PII), `n` (keep both), and `o` (omit PII only — strip the patient block but keep `AcquisitionDateTime`/`AcquisitionTime`). The `o` mode is what `reproinx.py` invokes so its `_scans.tsv` aggregation and fmap closest-time tie-breaker still have timestamps. `isOmitPiiBIDS` and `isAnonymizeBIDS` are independent flags in `TDCMopts`; `o` sets `isAnonymizeBIDS=false, isOmitPiiBIDS=true`.

The C code in `createDummyBidsBoilerplate(char *pth, bool isFunc, const char *taskName, const char *acqName)` in `nii_dicom_batch.cpp` writes the task sidecar with the *actual* entity set of the bold file (`task-<X>_bold.json` or `task-<X>_acq-<Y>_bold.json`). Keep this in lock-step with `spec.task`/`spec.acq` semantics in `reproin.cpp` — the legacy `%h` path passes `NULL/NULL` and still falls back to the historical `task-rest_bold.json` hardcoding. The post-pass must stay non-destructive on existing curated metadata: `README.md` → `README` cleanup is gated on `_is_dcm2niix_readme_stub`; generated root `task-X_bold.json` stubs are removed only when `_acq-` variants for the same task exist, and `TaskName` is written into each per-series BOLD sidecar first so bare task runs remain valid without triggering BIDS v2 multiple-inheritance errors. Hand-written sidecars with extra metadata and READMEs must survive re-runs untouched.

### Siemens SPACE / FLAIR detection in `setBidsSiemens`

Two related fixes for variable-flip-angle TSE FLAIR misclassification:
1. **The bold cascade's `pace` check is now anchored on `_pace`** ([nii_dicom_batch.cpp:~8079](console/nii_dicom_batch.cpp#L8079)). The pre-fix `strstr(seqDetails, "pace")` matched `"space"` as a substring (the comment "n.b. Space is not pace" was a warning, not enforcement). The fix matches `"_pace"` so the Siemens PACE (Prospective Acquisition Correction) sequences `ep2d_bold_PACE` / `ep2d_pace` still hit while `\space` does not.
2. **The `tse_vfl` branch now also fires on `\space` and consults `pulseSequenceName`** ([nii_dicom_batch.cpp:~7982](console/nii_dicom_batch.cpp#L7982)). Siemens SPACE (Sampling Perfection with Application-optimized Contrasts) is the same variable-flip-angle TSE family as `tse_vfl`, just the marketing name on XA-line scanners. The FLAIR override inside that branch now also checks `d->pulseSequenceName` for `"spcir"` because on XA-line Siemens `d->sequenceName` can be empty while the inversion-recovery signal lives in `pulseSequenceName` (e.g. `*spcir_220ns`).

Together: `/Users/chris/src/dcm_qa_xb10/flair/in/` with `seqDetails="%SiemensSeq%\\space"`, `pulseSequenceName="*spcir_220ns"` now correctly produces `["anat","_acq-spcir2p2_run-5_FLAIR"]` (was misclassified as `["func","_acq-spcir2p2_dir-RL_run-5_bold"]` via the `pace`-substring path).

### fl3d_vibe Ernst-angle physics classifier in `setBidsSiemens`

`fl3d_vibe` (Siemens VIBE - Volumetric Interpolated Breath-hold) can be tuned as T1w, PDw, or T2*-weighted depending on TR/TE/flip-angle, but the sequence name is constant ("vibe"). A new branch in the `setBidsSiemens` cascade fires on `strstr(seqDetails, "fl3d_vibe")` and classifies by physics rather than pattern: estimate T1 via Bottomley's approximation `0.8 * fieldStrength^0.38`, T2* via `0.050 / fieldStrength`, and the Ernst angle from `acos(exp(-TR/T1))`. Rules: `TE >= 0.5 * T2*` → T2starw; `flipAngle >= 1.3 * Ernst` → T1w; `flipAngle <= 0.7 * Ernst` → PDw; default PDw for mixed structural / edge cases. Guarded on positive TR / TE / fieldStrength / flipAngle — if any are missing, the cascade falls through to the existing `if (isDerived) dataTypeBIDS = "derived"` trailing clobber. Verified on `/Users/chris/src/dcm_qa_xb10/fx/10_t1_vibe_tra_cs22/` (3T, TR=5.18ms, TE=2.46ms, FA=20°): Ernst ≈ 5.3°, FA/Ernst = 3.8 → T1w.

### SWI ImageType override in `setBidsSiemens`

After the sequence-name cascade in `setBidsSiemens` (just before the `_acq-` entity construction in `nii_dicom_batch.cpp`), an `ImageType` token check fires: any series whose ImageType array contains `SWI` is classified as `anat/_T2starw` regardless of which sequence-name branch above matched, and `isDerived` is cleared so the trailing `if (isDerived) dataTypeBIDS = "derived"` clobber at the end of the function does not flip it back. Covers EPI-based SWI sequences (e.g. `*swi3d_epr` on Siemens XA80) that do not match the existing `fl3d` / `gre` / `ep_seg_fid` patterns, plus MINIMUM / SWI_Images derived projections. Test: `strstr(d->imageType, "_SWI")` — the underscore prefix ensures `SWI` matches as a token (DICOM ImageType is underscore-joined) and not as a substring of `SWIRL` etc. The pre-existing `ep_seg_fid` mIP / SWI_Images demote at line ~8031 is now reachable only when the SWI override doesn't fire (i.e. ImageType lacks `_SWI` but seriesDescription contains those tokens) — a narrow secondary path retained for back-compat.

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
