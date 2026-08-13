# dcm2niix v1.0.20260813 (13-August-2026)

## Breaking changes

- **3D sequence `RepetitionTime` (issue #1024).** For Siemens 3D-EPI (and related 3D) acquisitions, `RepetitionTime` now reports the true volume TR rather than the per-shot interval; the per-excitation value is emitted separately as `RepetitionTimeExcitation`. ASL (perfusion) series are excluded — their per-shot TR is retained and per-volume timing is carried by `PostLabelingDelay`. Pipelines that relied on the previous `RepetitionTime` for these sequences will see a different value.

## New features

- **MR Spectroscopy (MRS).** DICOM MR spectroscopy is now converted to the [NIfTI-MRS](https://github.com/wtclarke/mrs_nifti_standard) standard (header extension ecode 44), covering single-voxel (SVS) and CSI/MRSI for Siemens (classic and enhanced), Philips, and UIH, with BIDS-MRS sidecars. Includes `tools/mrs_post.py` for vendor-state post-processing.
- **ReproIn BIDS naming and `reproinx.py`.** New one-pass ReproIn filename output (`-f %H`) plus `tools/reproinx.py`, a cross-series post-processor that turns dcm2niix output into a validator-clean BIDS dataset — fieldmap `B0Field` linkage, multi-echo phase `part-` entities, session/run disambiguation, dataset scaffolding, and optional date shifting. The `-br` flag overrides the ReproIn project subfolder.
- **Physiological logging.** Siemens XA-line PhysioLogging and legacy CMRR PMU signals are decoded and written as BIDS physio sidecars (cardiac, respiratory, and trigger channels).
- **RF-off (noise) volumes (issue #1025).** Siemens RF-off calibration volumes are split from their imaging series and named with the BIDS `_noRF` suffix.
- **Multi-delay pCASL (Siemens XA `tgse_pcasl_loft`).** The LOFT multi-PLD pCASL research sequence is decoded to BIDS ASL (per-volume `PostLabelingDelay`, `_aslcontext.tsv`, dummy-volume removal via reproinx).
- **Multi-fragment encapsulated pixel data (issue #1017).** Single-frame codestreams split across multiple encapsulation fragments are now reassembled for JPEG 2000 / JPEG-LS decoding.
- **Expanded BIDS sidecars.** Additional fields including `MatrixCoilMode`, `PulseSequenceType`, and (for PET) `MolarActivity` and `AcquisitionTime`.

## Vendor / data fixes

- Refined BIDS classification: unified MR weighting heuristic and improved SWI, VIBE, SPACE/FLAIR, and ASL detection, with `AcquisitionContrast` as a vendor-agnostic fallback.
- Fixed orientation for volumes with more than 1024 slices (issue #1015).
- Hardened foreign-format writers (MGZ / NRRD): corrected `.mgz` output and fail-closed handling of truncated or partial files.
- `dcm2niix` now returns a non-zero exit code for invalid command-line options (issue #1020).
