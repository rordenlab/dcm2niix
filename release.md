# dcm2niix v1.0.20261029 (29-October-2026)

Changes since v1.0.20260724.

## Behaviour changes

- **`-ba y` redacts the CMRR measurement UUID from `WipMemBlock`.** `-ba y` is the default; use `-ba o` or `-ba n` to keep it.
- **Siemens 2D `AcquisitionTime`.** Reports the [earliest slice](https://github.com/rordenlab/dcm2niix/issues/1039) of the first volume (the origin of `SliceTiming`), not instance 1; date-aware across midnight.
- **Philips Dixon.** Water/fat/in-phase/out-phase are [separate series](https://github.com/rordenlab/dcm2niix/issues/1038) with `_water`/`_fat`/`_inphase`/`_outphase` postfixes and `rec-` entities in `BidsGuess` and `%h`/`%H` names.
- **Parametric Maps.** Multi-channel maps are [split by LUTLabel](https://github.com/rordenlab/dcm2niix/issues/1041) into one file per channel. [Validation Dataset](https://github.com/neurolabusc/dcm_qa_parametermap).
- **ASL `RepetitionTime`.** ASL keeps the per-shot TR; the issue 1024 volume-TR override no longer applies to perfusion series.
- **BIDS sidecars.** Siemens partial Fourier emits `PartialFourierDirection` `PHASE`; `TablePosition` prints `0` not `-0`; `NumberOfKSpaceTrajectories` is MRS-only; UIH `(0018,9082)` is read as EffectiveEchoSpacing, not echo time, and BWPE as per acquired pixel ([issue 1035](https://github.com/rordenlab/dcm2niix/issues/1035)).

## New features

- **Multi-delay pCASL.** Siemens XA `tgse_pcasl_loft` is decoded to BIDS ASL (per-volume `PostLabelingDelay`, `_aslcontext.tsv`, dummy-volume removal via reproinx).
- **`CMRRMeasurementUUID`** in CMRR imaging and physio sidecars (`-ba n` only), pairing physio with image series.
- **Series listing.** `-n -2` writes `dcm2niix_series.json` without converting; `-n -1` no longer touches files (previously `-n -1 -w 1` deleted existing outputs).
- **Browser package** `@niivue/dcm2niix` 1.4: `convert()`, `listSeries()`, `groupOutputs()`, `exitCodes`, shipped TypeScript declarations; one JPEG-LS/JPEG 2000 build (`/plain` and `/jpeg` are aliases); output names follow the input's shared top folder (loose files or several top-level folders still give `input_`); inputs are read lazily from the `File` (WORKERFS) with a read-ahead cache; WASM heap may grow to 4 GB and outputs are freed as collected.
- **Filename specifiers** `%@` (station name) and `%=` (device serial number).
- **BIDS classification.** Siemens T1w spin echo and T1 SPACE; MP2RAGE `RepetitionTimePreparation`; dzne_ep3d `ReadPolarity`.
- **ReproIn.** Stricter entity/bareword parsing, `ses-{date}`, `inv` entity; reproinx run correspondence and stale `B0Field` link cleanup.

## Fixes

- Malformed JPEG lossless, JPEG 2000 and JPEG-LS frames whose size disagrees with the DICOM header (including one codestream under a multi-frame header, or a mosaic split into frames) are rejected instead of over-reading memory or returning uninitialized pixels; TurboJPEG builds check the same; JPEG-LS and TurboJPEG no longer leak their input buffers.
- JPEG: lossless multi-frame images with several fragments per frame, Extended JPEG (.51) including 12-bit ([issue 1054](https://github.com/rordenlab/dcm2niix/issues/1054)), and encapsulated multi-frame files decoded at their own item offsets.
- GE MR30 DWI: VascularCollapseFlag 3/4/5 gradients and per-volume trace detection; clearer missing-image messages report the image total and spatial positions (thanks to [PR #1056](https://github.com/rordenlab/dcm2niix/pull/1056)).
- Faster conversion of many classic DICOM files: per-file parser setup no longer zeroes a 9 MB table (regression from the Parametric Map split).
- Compressed (JPEG-LS, JPEG 2000, RLE) enhanced DICOM keeps Dimension Index frame order and per-frame scaling; 4D JPEG lossless decodes all frames; RLE RGB planar configuration 1.
- Rename mode (`-r y`) fails closed on read/write errors; byte-identical inputs are skipped, true conflicts return exit 8.
- Stack overflows: macOS MRSI segfault (PR #1042) and Windows 32 MB stack reserve ([issue 1040](https://github.com/rordenlab/dcm2niix/issues/1040)).
- CMake superbuild forwards toolchain, macOS SDK and architectures; Cloudflare zlib pinned; OpenJPEG flags preserved on non-MSVC; Windows wheel build.
- pigz hint shown only in verbose mode ([issue 1027](https://github.com/rordenlab/dcm2niix/issues/1027)).
