# LittleCMS and dcm2niix

[LittleCMS](https://github.com/mm2/Little-CMS) 2.19.1 (MIT, see `LICENSE`) is compiled only into the WASM frame decoder (`console/makefile` target `wasm-lcms`, flag `myEnableLCMS`). It gives `nii_iccLut`, which bakes a DICOM-WSI ICC profile (0028,2000) into an sRGB lookup table that the viewer applies to every tile; native dcm2niix builds do not use it, and NIfTI output is never colour-converted.

Source: `lcms2-2.19.1.tar.gz` from the GitHub release, SHA-256 `bfc54f7bab59fbc921012014a8032e4cba4abd46db47d46b76416a8c0b2815c8` (the same tarball Homebrew's little-cms2 2.19.1 builds). To update, copy the release's `src/*.c`, `src/*.h` and `include/*.h` into this folder and `LICENSE` beside them. The folder is skipped by codespell (`.codespellrc`): upstream typos stay upstream.
