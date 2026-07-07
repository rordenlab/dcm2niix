# Build And Test Reference

Use this before touching CMake, release channels, Docker, PyPI, source-list fanout, test runners, or formatting.

## Commands

Full CMake: `mkdir build && cd build && cmake -DZLIB_IMPLEMENTATION=Cloudflare -DUSE_JPEGLS=ON -DUSE_OPENJPEG=ON .. && make`. Deployment channels use `-DZLIB_IMPLEMENTATION=zlib-ng -DUSE_JPEGLS=ON -DUSE_OPENJPEG=GitHub`; `GitHub` is required so OpenJPEG is static-by-contract. Makefile builds from `console/` via `make`, with targets `debug`, `sanitize`, `jp2`, `turbo`, `wasm`.

## Regression

In-tree regression:
```bash
git submodule update --init
export PATH="$PWD/build/bin:$PATH"
for d in dcm_qa dcm_qa_nih dcm_qa_uih; do (cd "$d" && ./batch.sh) || { echo "FAIL: $d"; exit 1; }; done
```

MRS regression is in sibling `dcm_qa_mrs`: `python3 batch.py --corpus={local,spec2nii,both}` and `python3 compare_spec2nii.py --corpus={local,spec2nii,both}`; `$SPEC2NII_DATA` points to the spec2nii test data clone. reproinx offline self-check: `python3 tools/test_reproinx_sbref.py`.

## Lint / Style

Pre-push minimum includes `dcm_qa` plus `git ls-files | xargs codespell`. C/C++ may be contributed without hand-matching local indentation; periodic formatting uses `clang-format -i -style="{BasedOnStyle: LLVM, IndentWidth: 4, IndentCaseLabels: false, TabWidth: 4, UseTab: Always, ColumnLimit: 0}" *.cpp *.h`. Markdown convention is no hard-wrapping.

## Packaging Gotchas

Repo-controlled release channels are PyPI `pyproject.toml`, GitHub release `.appveyor.yml`, CI `.github/workflows/build.yml`, and `Dockerfile`. conda-forge and NeuroDebian are external feedstocks. zlib-ng is pinned at `2.3.3` with `ZLIB_COMPAT=ON`; OpenJPEG is pinned at `2.5.3`.
