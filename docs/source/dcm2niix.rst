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

-b <y/n>        Save additional BIDS metadata to a side-car .json file.

-ba <y/n>       anonymize BIDS

-f <format>     Format string for the output filename(s). The following
                specifiers are supported:

                - %a, antenna (coil) number
                - %c, comments
                - %d, description
                - %e, echo number
                - %f, folder name
                - %i, patient ID
                - %j, seriesInstanceUID
                - %k, studyInstanceUID
                - %m, manufacturer
                - %n, patient name
                - %p, protocol
                - %s, series number
                - %t, time
                - %u, acquisition number
                - %v, vendor
                - %x, study ID
                - %z, sequence name

                The default format string is "%p_%e_%4s".

-i <y/n>        Ignore derived, localizer and 2D images.

-m <y/n>        Merge slices from the same series regardless of study time,
                echo, coil, orientation, etc...

-n <number>     Only convert this series number. Provide a negative number for
                listing of series numbers in input folder.

-o <path>       Output directory where the converted files should be saved. If
                unspecified, the files are saved within the specified source
                directory.

-p <y/n>        Use Philips precise float (rather than display) scaling.

-s <y/n>        Convert a single file only.

-t <y/n>        Save patient details.

-v <h/y/n>  	Enable verbose output. "n" for succinct, "y" for verbose, "h" for
                high verbosity

-x <y/n>        Crop images.

-z <y/i/n>      Desired compression method. The "y"es option uses the external
                program pigz if available. The "i" option compresses the image
                using the slower built-in compression routines.

Licensing
---------

Copying and distribution of this file, with or without modification, are
permitted in any medium without royalty provided the copyright notice and this
notice are preserved. This file is offered as-is, without any warranty.
