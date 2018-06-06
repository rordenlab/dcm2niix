[![Build status](https://ci.appveyor.com/api/projects/status/yq0naf3v2m8nfa8r/branch/master?svg=true)](https://ci.appveyor.com/project/vbaderks/charls/branch/master)
[![Build Status](https://travis-ci.org/team-charls/charls.svg?branch=master)](https://travis-ci.org/team-charls/charls)

# CharLS

CharLS is a C++ implementation of the JPEG-LS standard for lossless and near-lossless image compression and decompression.
JPEG-LS is a low-complexity image compression standard that matches JPEG 2000 compression ratios.

# CharLS and dicm2niix

[CharLS](https://github.com/team-charls/charls) is an optional module for dcm2niix. If included, it allows dcm2niix to handle the   [JPEG-LS transfer syntaxes](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#DICOM_Transfer_Syntaxes_and_Compressed_Images). The included code was downloaded from the CharLS website on 6 June 2018. To enable support you will need to include the `myEnableJPEGLS` compiler flag as well as a few file sin the `charls` folder. Therefore, a minimal compile should look like this:

```
g++ -I. -DmyEnableJPEGLS  charls/jpegls.cpp charls/jpegmarkersegment.cpp charls/interface.cpp  charls/jpegstreamwriter.cpp charls/jpegstreamreader.cpp main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp  -o dcm2niix -DmyDisableOpenJPEG
```
You can use gdcmconv to compare the performance of the ancient JPEG-lossless (gdcmconv -J), JPEG-LS (gdcmconv -L) and JPEG2000-lossess (gdcmconv -K). Below is a sample test looking at 800 DICOM CT scans - with a raw size of 425mb which dcm2niix can convert in 1.6 seconds. The table shows that JPEG-LS reduces the file sizes to 137mb (0.39 original size), but that decompression takes 7.2 times longer. In contrast, the complicated JPEG2000 achieves only slightly better compression but is much slower to decompress.

| Tables                                    | Speed | Size  |
| ----------------------------------------- | -----:| -----:|
| Raw 1.2.840.10008.1.2.1                   |  1.00 |  1.0  |
| JPEG-lossless 1.2.840.10008.1.2.4.70      |  0.39 |  5.6  |
| JPEG-LS 1.2.840.10008.1.2.4.80            |  0.32 |  7.2  |
| JPEG2000 lossless 1.2.840.10008.1.2.4.90  |  0.31 | 60.1  |

## Features

* C++14 library implementation with a binary C interface for maximum interoperability.</br>Note: a C++03 compatible implementation is maintained in the 1.x-master branch.
* Supports Windows, Linux and Solaris in 32 bit and 64 bit.
* Includes an adapter assembly for .NET based languages.
* Excellent compression and decompression performance.

## About JPEG-LS

JPEG-LS (ISO/IEC 14495-1:1999 / ITU-T.87) is an image compression standard derived from the Hewlett Packard LOCO algorithm. JPEG-LS has low complexity (meaning fast compression) and high compression ratios, similar to the JPEG 2000 lossless ratios. JPEG-LS is more similar to the old Lossless JPEG than to JPEG 2000, but interestingly the two different techniques result in vastly different performance characteristics.
Wikipedia on lossless JPEG and JPEG-LS: <http://en.wikipedia.org/wiki/Lossless_JPEG>
Tip: the ITU makes their version of the JPEG-LS standard (ITU-T.87) freely available for download, the text is identical with the ISO version.

## About this software

This project's goal is to provide a full implementation of the ISO/IEC 14495-1:1999, "Lossless and near-lossless compression of continuous-tone still images: Baseline" standard. This library is written from scratch in portable C++. The master branch uses modern C++14. The 1.x branch is maintained in C++03. All mainstream JPEG-LS features are implemented by this library.
According to preliminary test results published on http://imagecompression.info/gralic, CharLS is about *twice as fast* as the original HP code, and beats both JPEG-XR and JPEG 2000 by a factor 3.

### Limitations

* No support for (optional) JPEG restart markers (RST). These markers are rarely used in practice.
* No support for the SPIFF file header.
* No support for oversize image dimension. Maximum supported image dimensions are [1, 65535] by [1, 65535].
* After releasing the original baseline standrd 14495-1:1999, ISO released an extension to the JPEG-LS standard called ISO/IEC 14495-2:2003: "Lossless and near-lossless compression of continuous-tone still images: Extensions". CharLS doesn't support these extensions.

## Supported platforms

The code is regularly compiled/tested on Windows and 64 bit Linux. Additionally, the code has been successfully tested on Linux Intel/AMD 32/64 bit (slackware, debian, gentoo), Solaris SPARC systems, Intel based Macs and Windows CE (ARM CPU, emulated), where the less common compilers may require minor code edits. It leverages C++ language features (templates, traits) to create optimized code, which generally perform best with recent compilers. If you compile with GCC, 64 bit code performs substantially better.

## Users & Acknowledgements

CharLS is being used by [GDCM DICOM toolkit](http://sourceforge.net/projects/gdcm/), thanks for [Mathieu Malaterre](http://sourceforge.net/users/malat) for getting CharLS started on Linux. [Kato Kanryu](http://knivez.homelinux.org/) wrote an initial version of the color transfroms and the DIB output format code, for an [irfanview](http://www.irfanview.com) plugin using CharLS. Thanks to Uli Schlachter, CharLS now finally runs correctly on big-endian architectures like Sun SPARC.

## Legal

The code in this project is available through a BSD style license, allowing use of the code in commercial closed source applications if you wish. **All** the code in this project is written from scratch, and not based on other JPEG-LS implementations. Be aware that Hewlett Packard claims to own patents that apply to JPEG-LS implementations, but they license it for free for conformant JPEG-LS implementations. Read more at <http://www.hpl.hp.com/loco/> before you use this if you use this code for commercial purposes.
