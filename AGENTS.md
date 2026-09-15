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
- Release validation uses sibling `dcm_validate`; edge cases, validation data, and regression tests belong there rather than in this root project. Pre-push minimum is `dcm_qa` plus `git ls-files | xargs codespell`.
- CMRR physio + `CMRRMeasurementUUID` regression in sibling `dcm_qa_physio`: `./batch.sh`.
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
- `cmrr_uuid.h` is a header-only helper parsing the CMRR measurement UUID (Phoenix `sWipMemBlock.tFree` or physio payload header) for the `CMRRMeasurementUUID` sidecar key, and redacting it from `WipMemBlock` under `-ba y`.
- Adding a `.cpp` requires updating all source-list surfaces: `console/CMakeLists.txt` (3 blocks), `console/makefile`, `console/windows.bat`, `console/notarize.sh`, `COMPILE.md`.

## Project Conventions

- `TDICOMdata` is passed by value through the save chain; growing it by about 4 KB can crash macOS stacks. New per-file payloads use lazy heap pointers plus idempotent `free_TDICOMdata_*` helpers at every `free(dcmList)` site.
- `deID_CS` is owned only by the `dcmList[]` entry from `readDICOMx`; shallow retained copies must set `.deID_CS = NULL; .deID_CS_n = 0`; direct `readDICOM()` callers must free it.
- Inline `TDICOMdata` additions must be tiny; `slabOrient[7] + slabOrientCount` (+32 B) is the accepted scale, not a precedent for arrays.
- Stack budget: macOS (`AppleClang` gate), Windows (`/STACK:16388608`) and WASM (`STACK_SIZE=16MB`) all set 16 MB explicitly; only Linux/BSD run on the 8 MB default, and there linker flags are ignored so users need `setrlimit`. Keep all three gates.
- The dominant stack consumer is `struct TDTI4D dti4D_local` in `saveDcm2NiiMRS`: `sizeof(TDTI4D)` is 7.78 MB where `kMaxSlice2D` is 98303 and 5.19 MB on Linux (65535), and it inlines into `saveDcm2Nii`, so it is charged to *every* conversion, MRS or not. That leaves the deepest chain (`nii_loadDir` -> `nii_loadDirCore` -> `saveDcm2Nii` -> `saveDcm2NiiCore` -> `nii_SaveBIDSX`) about 2.4 MB clear on Linux. A `static` helper inlines into `saveDcm2NiiCore` and is likewise charged to every conversion, so heap a large buffer there (a `kDICOMStrExtraLarge` 64 KB `wipMemBlock` is the trap) rather than declaring an array. Verify with `clang++ -O3 -fstack-usage -c console/nii_dicom_batch.cpp`.
- The `readKey*` family scans from `keyPos` (the `memmem` hit) but takes `remLength` for the whole buffer, so every loop must bound on `remLength - (keyPos - buffer)`; using `remLength` directly runs off the end of the CSA allocation. `readKeyStr` hardcodes a `kDICOMStrLarge` write bound regardless of the caller's array, and `siemensCsaAscii` fills `wipMemBlock` with a `kDICOMStrExtraLarge` bound, so destinations must be sized to the bound, not to the expected value.
- `json_Str` escapes into a heap buffer sized `2 * strlen + 1`; every input byte can expand to two. Do not put that buffer back on the stack, `sWipMemBlock.tFree` reaches 64 KB.
- `-ba` levels are not interchangeable: `-ba y` strips dates, patient PII *and* `SeriesInstanceUID`/`StudyInstanceUID` as persistent source links; `-ba o` strips PII only and deliberately keeps those UIDs. Gate a new source-linking key on `isAnonymizeBIDS` alone unless there is a reason to be stricter. For CMRR sequences the same measurement UUID leads `WipMemBlock`, so key-level suppression alone does not remove the value: `-ba y` also redacts it there, while `-ba o` and `-ba n` emit `WipMemBlock` verbatim.
- Output writers must fail closed: check short writes/close/compressor status, remove partial files, and never emit sidecars next to missing/truncated images.
- `writeNiiGz`/`writeMghGz` return status and never free caller-owned buffers; `writeMghGz` must `Z_FINISH` on the footer, not the image.
- `initTDTI4D()` is required for fresh sidecar-only/MRS locals, but must NOT be called in `saveDcm2NiiCore` where enhanced/PAR per-frame data is already live.

## Git

- `master` is stable releases only; PRs target `development`.
