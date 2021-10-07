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

-b <y/i/n>      Save additional BIDS metadata to a side-car .json file (default y).
                The "i"nput-only option reads DICOMs but saves neither BIDS nor NIfTI.

-ba <y/n>       anonymize BIDS (default y).
                If "n"o, side-car may report patient name, age and weight.

-f <format>     Format string for the output filename(s). The following
                specifiers are supported:

                - %a, antenna (coil) name
                - %b, basename (filename of 1st DICOM file)
                - %c, comments
                - %d, description
                - %e, echo number
                - %f, folder name
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

                The default format string is "%p_%e_%4s".

-g <y/n/o/i>    Generate defaults file (default n)
                If "y", create default file on completion
                If "n", default will not be written
                If "o", only reset and write defaults
                If "i", the values of the defaults file are ignored
                : reset defaults], default n)

-h              Show help

-i <y/n>        Ignore derived, localizer and 2D images (default n)

-l <y/n/o>      Losslessly scale 16-bit integers to use maximal dynamic range (default o).
                If "y", then intensity rescaled to use full 16-bit range.
                If "n", data not scaled uint16 will be saved as int16.
                If "o", original data and datatype preserved.

-m <y/n/2>      Merge slices from the same series regardless of study time,
                echo, coil, orientation, etc. (default 2).
                If "2", automatic based on image modality.

-n <number>     Only convert this series CRC number. Provide a negative number for
                listing of series CRC numbers in input folder.

-o <path>       Output directory where the converted files should be saved. If
                unspecified, the files are saved within the specified source
                directory.

-p <y/n>        Use Philips precise float (rather than display) scaling.

-r <y/n>        Rename instead of convert DICOMs. Useful for organizing images.

-s <y/n>        Convert a single file only.

-t <y/n>        Save patient details as text notes.

-u              Update check: attempts to see if newer version is available.

-v <2/y/n>  	Enable verbose output. "n" for succinct, "y" for verbose, "2" for
                high verbosity

-x <y/n/i>      Crop images. This will attempt to remove excess neck from 3D acquisitions.
                If "i", images are neither cropped nor rotated to canonical space.

-z <y/i/n>      Desired compression method. The "y"es option uses the external
                program pigz if available. The "i" option compresses the image
                using the slower built-in compression routines.

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
