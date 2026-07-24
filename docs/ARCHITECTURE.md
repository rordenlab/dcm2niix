# Architecture Reference

Use this when a change crosses module boundaries, changes save-chain behavior, or adds/removes source files.

## Core Flow

`main_console.cpp` parses CLI and orchestrates conversion. `nii_dicom_batch.cpp` scans directories, groups series, assembles 3D/4D images, writes BIDS sidecars, and owns physio/ReproIn/MRS special paths. `nii_dicom.cpp` parses DICOM into `TDICOMdata` and owns most vendor-specific tag behavior. `nifti1_io_core.cpp` writes NIfTI/JSON/reorientation. `nii_foreign.cpp` handles PAR/REC. `nii_ortho.cpp` handles reorientation/cropping/resampling. `dicom_fragments.{h,cpp}` owns multi-fragment single-frame encapsulation. `reproin.cpp` is the C-side one-pass `-f %H` parser; `tools/reproinx.py` is the cross-series finisher.

## Load-Bearing Ownership

`TDICOMdata` is large and passed by value through `saveDcm2NiiCore -> nii_loadImgXL -> headerDcm2Nii -> headerDcm2Nii2 -> headerDcm2NiiSForm`; new nontrivial per-file payloads must be heap-owned and freed at every `free(dcmList)` site. `deID_CS` is the worked ownership model: owned by `dcmList[]`, shallow copied elsewhere only after nulling the retained pointer. `TDTI4D` uses sentinel arrays for BIDS/PET emissions; initialize fresh locals, but do not clobber live enhanced/PAR data in `saveDcm2NiiCore`.

## Source Lists

Adding a `.cpp` requires all source-list surfaces to agree: `console/CMakeLists.txt` (three blocks), `console/makefile`, `console/windows.bat`, `console/notarize.sh`, and `COMPILE.md`.

## Related References

Build and packaging details live in `docs/BUILD.md`; parser hazards live in `docs/PARSER_GOTCHAS.md`; BIDS/ReproIn hazards live in `docs/BIDS_REPROIN.md`; MRS hazards live in `docs/MRS.md`.
