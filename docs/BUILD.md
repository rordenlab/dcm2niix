# Build And Test Reference

Use this before touching CMake, release channels, Docker, PyPI, npm/WASM, source-list fanout, test runners, or formatting.

## Commands

Full CMake: `mkdir build && cd build && cmake -DZLIB_IMPLEMENTATION=Cloudflare -DUSE_JPEGLS=ON -DUSE_OPENJPEG=ON .. && make`. Deployment channels use `-DZLIB_IMPLEMENTATION=zlib-ng -DUSE_JPEGLS=ON -DUSE_OPENJPEG=GitHub`; `GitHub` is required so OpenJPEG is static-by-contract. Makefile builds from `console/` via `make`, with targets `debug`, `sanitize`, `jp2`, `turbo`, `wasm`.

## Regression

In-tree regression:
```bash
git submodule update --init
export PATH="$PWD/build/bin:$PATH"
for d in dcm_qa dcm_qa_nih dcm_qa_uih; do (cd "$d" && ./batch.sh) || { echo "FAIL: $d"; exit 1; }; done
```

MRS regression is in sibling `dcm_qa_mrs`: `python3 batch.py --corpus={local,spec2nii,both}` and `python3 compare_spec2nii.py --corpus={local,spec2nii,both}`; `$SPEC2NII_DATA` points to the spec2nii test data clone. Philips Dixon (issue 1038) regression is in sibling `dcm_validate/dcm_qa_dixon`: `./batch.sh`. reproinx offline self-check: `python3 tools/test_reproinx_sbref.py`.

BIDS validation: `uvx bids-validator-deno@3.0.2 <dir>`; Homebrew `bids-validator` 1.15.0 on PATH is the legacy validator.

`dcm_validate` Ref refresh: gzipped-Ref suites (`batch.sh` runs `gzip -d` in place) leave unpacked copies, and stale untracked plain files shadow restored `.gz`, failing later runs with `Only in Ref: *.gz`; delete them. Zip-input suites (`dcm_qa_cs_dl`, `dcm_qa_mprage`) stall on the unzip overwrite prompt if `In/` was already unpacked; `git clean -fd In` first. Push order: `dcm_validate` submodule repos -> `dcm_validate` -> dcm2niix `development` (CI fetches submodule commits).

To run a suite with a specific build: `exenam=/path/dcm2niix ./batch.sh`. Most sibling `batch.sh` read `${examnam:-dcm2niix}` (typo), silently ignoring the override and using `dcm2niix` on PATH; fixed only in `dcm_qa_jpg`.

Browser package (`js/`, `@niivue/dcm2niix`): one build with OpenJPEG + CharLS (`console/makefile` `wasm-jpeg`, `-s ENVIRONMENT=web,worker`); `/jpeg` and `/plain` are export aliases, and `make wasm` is a C-side check only. `pixi run wasm` builds OpenJPEG and `js/dist` with the pinned emcc; plain `npm run build` reruns the WASM compile via `prebuild`, so use it only inside `pixi shell` (Homebrew emcc is the wrong version). Release: `pixi run pack-npm` (build, `npm test`, `npm run test:consumer`, `npm pack`), then publish that tarball by hand (`npm publish <tgz> --tag rc|latest`); `prepack` runs `scripts/check-dist.mjs`, which catches stale WASM/bundles but not same-version staleness, so release from a clean pinned rebuild. Tests need `git submodule update --init` and `npx playwright install chromium`. Input: `worker-core.js` mounts Files on WORKERFS (no copy) and patches its `read` to serve files ≤64 MB from whole-file `FileReaderSync` reads, batching neighbours ≤8 MB in `FS.readdir` walk order, 256 MB LRU cleared per run (issue 1052): each uncached read costs milliseconds and dcm2niix reads a file in many pieces and opens it three times (one uncached 9 MB file cost 700 ms). `collectOutput` reads each output via MEMFS internals (`FS.filesystems.MEMFS.getFileDataAsTypedArray`, a zero-copy view) and unlinks it once wrapped in a `File`; emcc flag changes (e.g. `--closure`, dropping `FS` from `EXPORTED_RUNTIME_METHODS`, moving `OUT_DIR` off MEMFS) must keep that path working. WASM magnifies per-file work that is cheap natively (a per-file `calloc` of the `kMaxSlice2D` `dcmDim` table made 618-file conversion 50% slower in WASM, 11% natively), so compare the `npm test` timing line after parser changes.

## Lint / Style

Pre-push minimum includes `dcm_qa` plus `git ls-files | xargs codespell`. C/C++ may be contributed without hand-matching local indentation; periodic formatting uses `clang-format -i -style="{BasedOnStyle: LLVM, IndentWidth: 4, IndentCaseLabels: false, TabWidth: 4, UseTab: Always, ColumnLimit: 0}" *.cpp *.h`. Markdown convention is no hard-wrapping.

## Packaging Gotchas

Repo-controlled release channels are PyPI `pyproject.toml`, GitHub release `.appveyor.yml`, CI `.github/workflows/build.yml`, and `Dockerfile`. conda-forge and NeuroDebian are external feedstocks. zlib-ng is pinned at `2.3.3` with `ZLIB_COMPAT=ON`; OpenJPEG is pinned at `2.5.3`. Windows stack overflow (0xC00000FD) is silent: no stderr, no output. The reserve is fixed at link time, so every Windows build path (CMake MSVC/MinGW, `windows.bat`, COMPILE.md `cl`) must carry the 32 MB `/STACK`/`--stack` (issue 1040).
