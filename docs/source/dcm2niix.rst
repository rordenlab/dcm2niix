:orphan:

dcm2niix manual
===============

Synopsis
--------

**dcm2niix** [*options*] <*sourcedir*>


Description
-----------

Most medical imaging devices save images in some variation of the popular DICOM
format. However, most scientific tools expect medical images to be stored in
the comparatively simpler NIfTI format. **dcm2niix** is designed to perform
such conversion from DICOM to NIfTI with a simple command-line interface.

Please be advised that **dcm2niix** has been developed for research purposes
only and should not be considered a clinical tool.


Options
-------

-1..-9          gz compression level (1=fastest..9=smallest, default 6)

-a <n/y>        Adjacent DICOMs (default n). If "y", assume all images from a
                series are in the same folder, for faster conversion.

-b <y/n/o>      Save additional BIDS metadata to a side-car .json file (default y).
                The "o"nly option reads DICOMs and writes the BIDS side-car but
                no NIfTI image.

-ba <y/n/o>     Anonymize BIDS (default y). "y" strips dates and PII; "n" keeps
                both; "o" strips PII only (keeps timestamps).

-c <comment>    Comment stored in the NIfTI aux_file (up to 24 characters, e.g.
                ``-c VIP``). Pass an empty string (``-c ""``) to anonymize tag
                (0020,4000).

-d <0..9>       Directory search depth: convert DICOMs in sub-folders of the
                input folder (default 5).

-e <y/n/o/j/b>  Export as NRRD ("y"), MGH ("o"), JSON/JNIfTI ("j") or BJNIfTI
                ("b") instead of NIfTI (default n).

-f <format>     Format string for the output filename(s). The following
                specifiers are supported:

                - %a, antenna (coil) name
                - %b, basename (filename of 1st DICOM file)
                - %c, comments
                - %d, description
                - %e, echo number
                - %f, folder name
                - %g, accession number
                - %i, patient ID
                - %j, series instance UID
                - %k, study instance UID
                - %m, manufacturer
                - %n, patient name
                - %o, media object instance UID
                - %p, protocol
                - %r, instance number (of 1st DICOM file)
                - %s, series number
                - %t, time
                - %u, acquisition number
                - %v, vendor
                - %x, study ID
                - %z, sequence name

                The default format string is "%f_%p_%t_%s". The special
                specifiers %h (legacy hierarchical BIDS) and %H (one-pass
                ReproIn BIDS) generate BIDS-style directory layouts; see
                REPROIN.md.

-g <y/n/o/i>    Generate defaults file (default n)
                If "y", create default file on completion
                If "n", default will not be written
                If "o", only reset and write defaults
                If "i", the values of the defaults file are ignored

-h              Show help

-i <y/n/o>      Ignore derived, localizer and 2D images (default n). "o"
                overrides to also discard non-planar localizers.

-l <y/n/o>      Losslessly scale 16-bit integers to use maximal dynamic range (default o).
                If "y", then intensity rescaled to use full 16-bit range.
                If "n", data not scaled uint16 will be saved as int16.
                If "o", original data and datatype preserved.

-m <n/y/2>      Merge slices from the same series regardless of study time,
                echo, coil, orientation, etc. (default 2).
                If "2", automatic based on image modality.

-n <number>     Only convert this series CRC number (may be repeated up to 16
                times). Provide a negative number to list the series CRC numbers
                in the input folder.

-o <path>       Output directory where the converted files should be saved. If
                unspecified, the files are saved within the specified source
                directory.

-p <y/n/o>      Use Philips precise float (rather than display) scaling (default
                y). "o" overrides and ignores variable intensity scaling.

-q <y/l/n>      Only search the directory for DICOMs (default y). "y" reports the
                number found, "l" additionally lists them, "n" disables.

-r <y/n>        Rename instead of convert DICOMs. Useful for organizing images.

-s <y/n>        Convert a single file only.

-t <y/n>        Save patient details as text notes.

-u              Update check: attempts to see if newer version is available.

-v <0/1/2>      Enable verbose output (default 0). "0"/"n" succinct, "1"/"y"
                verbose, "2" high verbosity.

-w <0/1/2>      Write behavior for name conflicts (default 2). "0" skips
                duplicates, "1" overwrites, "2" adds a suffix.

-x <y/n/i>      Crop images. This will attempt to remove excess neck from 3D acquisitions.
                If "i", images are neither cropped nor rotated to canonical space.

-z <y/o/i/n/3>  Desired compression method (default n). "y" uses the external
                program pigz if available, "o" optimal pigz, "i" the slower
                built-in (miniz) routine, "n" no compression, "3" no
                compression for 3D output.

--big-endian <y/n/o>     Byte order (default o). Optimal is machine native

--progress               Slicer format progress information (y/n, default n)

--ignore_trigger_times   Disregard values in 0018,1060 and 0020,9153

--terse                  Omit filename post-fixes (can cause overwrites)

--version                Report version and terminate

--xml                    Slicer format features

Licensing
---------

Copying and distribution of this file, with or without modification, are
permitted in any medium without royalty provided the copyright notice and this
notice are preserved. This file is offered as-is, without any warranty.
The dcm2niix project is distributed under the BSD 2-Clause License.
