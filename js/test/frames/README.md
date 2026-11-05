# Frame-decoder fixtures

JPEG 2000 codestreams (one DICOM-WSI frame each, item payload only) for `createFrameDecoder` tests in `../browser.mjs`.

| File | Source | Licence | Exercises |
|---|---|---|---|
| `ict.j2k` | OpenSlide test data, `CMU-1-JP2K-ICT-v2` (DICOM conversion of CMU-1.svs), level 0 | CC0-1.0 | lossy 9/7, YBR_ICT, MCT applied by the codec |
| `rct.j2k` | OpenSlide test data, `CMU-1-JP2K-RCT-v2`, level 0 | CC0-1.0 | lossless 5/3, YBR_RCT, MCT applied by the codec |
| `ybr422.j2k` | synthetic, from `rct.j2k` | CC0-1.0 | YCbCr 4:2:2 (chroma dx=2) with no MCT, as YBR_FULL_422 WSI frames are |

`expected.json` holds each frame's RGBA SHA-256, sample pixels (`pixel0`, `pixelMid`) and channel means (`meanRGB`) from the native `decode_frames` harness (built unfused, see `docs/PARSER_GOTCHAS.md`). Lossless frames must match the SHA-256 exactly. Lossy 9/7 decodes in floating point and WASM OpenJPEG 2.5.3 differs from native 2.5.4 by up to 1, so `ict` (`"lossy": true`) is checked by its two sample pixels (within 1) and per-channel means (within 0.05; a double colour conversion moves a mean by about 26). The test also asserts `mct`/`isYCbCr`: ICT inverted by the codec and not converted again, 4:2:2 converted from YCbCr.

Never hand-edit these files: `dcm_validate/wsi_frames/make_js_fixtures.py` regenerates all of them (requirements and order in its README) from the OpenSlide corpus (https://openslide.cs.cmu.edu/download/openslide-testdata/DICOM/). `ict.j2k` and `rct.j2k` are copied byte for byte; `ybr422.j2k` is re-encoded by `opj_compress` (Homebrew OpenJPEG 2.5.4), so another OpenJPEG may give different bytes. Frame numbers and source checksums are in `expected.json`.
