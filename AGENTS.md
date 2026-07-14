## Prime Directive

dcm2niix handles real scanner DICOMs, including malformed/vendor-specific data. Assume quirks are load-bearing. Make surgical changes only. Do not refactor, simplify, reorder, or remove adjacent logic unless you understand the real dataset it protects. Correctness means handling scanner output, not just the DICOM standard. When unsure, leave it.

## Reference Docs

Domain hazards live in on-demand docs — read the relevant one before changing that area. Each is the authoritative gotcha list for its domain; keep new gotchas there, not here.

- `docs/ARCHITECTURE.md` — module boundaries, save-chain ownership, source-list fanout. Read before broad module/save-chain changes.
- `docs/BUILD.md` — CMake, CI, Docker, PyPI, test runners, formatting. Read before changing any of those.
- `docs/PARSER_GOTCHAS.md` — DICOM tag descent, transfer syntax, encapsulation, slice geometry, vendor-private parsing.
- `docs/BIDS_REPROIN.md` — BIDS sidecars, physio output, classification, `BidsGuess`, `-f %h/%H`, `reproin.cpp`, `tools/reproinx.py`.
- `docs/MRS.md` — MR Spectroscopy, NIfTI-MRS ecode 44, `tools/mrs_post.py`.
- `docs/siemens.md` — reference table for `setBidsSiemens()` BIDS-guess cascade + the `MRWeightingGuess()` T1/PD/T2/FLAIR heuristic.

## Build / Test / Lint

- Full CMake: `mkdir build && cd build && cmake -DZLIB_IMPLEMENTATION=Cloudflare -DUSE_JPEGLS=ON -DUSE_OPENJPEG=ON .. && make` -> `build/bin/dcm2niix`.
- Deployment build standard for repo-controlled channels: `-DZLIB_IMPLEMENTATION=zlib-ng -DUSE_JPEGLS=ON -DUSE_OPENJPEG=GitHub`; zlib-ng `2.3.3` and OpenJPEG `2.5.3` are static pinned source builds; use `GitHub`, not `ON`, for OpenJPEG.
- Makefile: `cd console && make`; targets include `debug`, `sanitize`, `jp2`, `turbo`, `wasm`.
- CMake options: `-DZLIB_IMPLEMENTATION=Miniz|System|Cloudflare|zlib-ng|Custom`, `-DUSE_OPENJPEG=OFF|GitHub|System`, `-DUSE_JPEGLS=ON|OFF`, `-DUSE_TURBOJPEG=ON|OFF`, `-DUSE_ZSTD=ON|OFF`, `-DBATCH_VERSION=ON`.
- In-tree regression:
  ```bash
  git submodule update --init
  export PATH="$PWD/build/bin:$PATH"
  for d in dcm_qa dcm_qa_nih dcm_qa_uih; do (cd "$d" && ./batch.sh) || { echo "FAIL: $d"; exit 1; }; done
  ```
- Release validation uses sibling `dcm_validate`; pre-push minimum is `dcm_qa` plus `git ls-files | xargs codespell`.
- MRS regression in sibling `dcm_qa_mrs`: `python3 batch.py --corpus={local,spec2nii,both}`, `python3 compare_spec2nii.py --corpus={local,spec2nii,both}`; `$SPEC2NII_DATA` must point at spec2nii test data.
- reproinx self-check: `python3 tools/test_reproinx_sbref.py`.
- Format C/C++ when needed: `clang-format -i -style="{BasedOnStyle: LLVM, IndentWidth: 4, IndentCaseLabels: false, TabWidth: 4, UseTab: Always, ColumnLimit: 0}" *.cpp *.h`.
- C/C++ indentation may be inconsistent; contributors need not match local style by hand because periodic clang-format normalizes it.
- Markdown convention: no hard-wrapping.
- `temp/` is PHI scratch; clean it after cycles that write there.

## Architecture Map

- All core C/C++ source is in `console/`; data flow is `main_console.cpp` -> `nii_dicom_batch.cpp` -> `nii_dicom.cpp` -> `nii_dicom_batch.cpp` -> `nifti1_io_core.cpp`.
- `nii_dicom.cpp` parses DICOM and vendor tags into `TDICOMdata`; most scanner gotchas live here.
- `nii_dicom_batch.cpp` groups series, assembles volumes, writes NIfTI/BIDS, physio, reproin, and MRS paths; treat it as load-bearing.
- `nifti1_io_core.cpp` writes NIfTI/JSON/reorientation; `nii_foreign.cpp` handles PAR/REC; `nii_ortho.cpp` handles orientation/crop/resample.
- `dicom_fragments.{h,cpp}` is the multi-fragment single-frame encapsulation helper for issue #1017.
- Adding a `.cpp` requires updating all source-list surfaces: `console/CMakeLists.txt` (3 blocks), `console/makefile`, `console/windows.bat`, `console/notarize.sh`, `COMPILE.md`.

## Project Conventions

- `TDICOMdata` is passed by value through the save chain; growing it by about 4 KB can crash macOS stacks. New per-file payloads use lazy heap pointers plus idempotent `free_TDICOMdata_*` helpers at every `free(dcmList)` site.
- `deID_CS` is owned only by the `dcmList[]` entry from `readDICOMx`; shallow retained copies must set `.deID_CS = NULL; .deID_CS_n = 0`; direct `readDICOM()` callers must free it.
- Inline `TDICOMdata` additions must be tiny; `slabOrient[7] + slabOrientCount` (+32 B) is the accepted scale, not a precedent for arrays.
- macOS CMake and makefile builds intentionally set 16 MB stack; keep the `AppleClang` gate.
- Output writers must fail closed: check short writes/close/compressor status, remove partial files, and never emit sidecars next to missing/truncated images.
- `writeNiiGz`/`writeMghGz` return status and never free caller-owned buffers; `writeMghGz` must `Z_FINISH` on the footer, not the image.
- `initTDTI4D()` is required for fresh sidecar-only/MRS locals, but must NOT be called in `saveDcm2NiiCore` where enhanced/PAR per-frame data is already live.

## Git

- `master` is stable releases only; PRs target `development`.
