[![Build Status](https://travis-ci.org/rordenlab/dcm2niix.svg?branch=master)](https://travis-ci.org/rordenlab/dcm2niix)
[![Build status](https://ci.appveyor.com/api/projects/status/xdkqua54f90x4049/branch/master?svg=true)](https://ci.appveyor.com/project/chrisfilo/dcm2niix)

## About

dcm2niix is a designed to convert neuroimaging data from the DICOM format to the NIfTI format. For details and compiled versions visit the [NITRC wiki](http://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage).

## License

This software is open source. The bulk of the code is covered by the BSD license. Some units are either public domain (nifti*.*, miniz.c) or use the MIT license (ujpeg.cpp). See the license.txt file for more details.

## Versions

7-Feb-2017
 - Handle 3D Philips DICOM and [PAR/REC](https://www.nitrc.org/forum/forum.php?thread_id=7707&forum_id=4703) files where images are not stored in a spatially contiguous order.
 - Handle DICOM violations where icon is uncompressed but image data is compressed.
 - Best guess matrix for 2D slices (similar to dcm2nii, SPM and MRIconvert).
 - Linux (case sensitive filenames) now handles par/rec as well as PAR/REC.
 - Images with unknown phase encoding do not generate [BIDS entry](https://github.com/rordenlab/dcm2niix/issues/79).
 - Unified printMessage/printWarning/printError aids embedding in other projects, such as [divest](https://github.com/jonclayden/divest).

1-Nov-2016
 - AppVeyor Support (Ningfei Li & Chris Filo Gorgolewski)
 - Swap 3rd/4th dimensions for GE sequential multi-phase acquisitions (Niels Janssen).

10-Oct-2016
 - Restores/improves building for the Windows operating system using MinGW.

30-Sept-2016
 - Save ImageType (0x0008,0x0008) to BIDS.
 - Separate CT scans with different exposures.
 - Fixed issues where some compilers would generate erratic filenames for zero-padded series (e.g. "-f %3s").

21-Sept-2016
 - Reduce verbosity (reduce number of repeated warnings, less scary warnings for derived rather than raw images).
 - Re-enable custom output directory "-o" option broken by 30-Apr-2016 version.
 - Deal with mis-behaved GE CT images where slice direction across images is not consistent.
 - Add new BIDS fields (field strength, manufacturer, etc).
 - Philips PAR/REC conversion now reports inconsistent requested vs measured TR (due to prospect. motion corr.?).
 - GE: Locations In Acquisition (0054, 0081) is inaccurate if slices are interpolated, use Images In Acquisition (0020,1002) if available.
 - New filename options %d Series description (0008,103E), %z Sequence Name (0018,0024).
 - New filename options %a antenna (coil) number, %e echo number.
 - Initialize unused portions of NIfTI header to zero so multiple runs always produce identical results.
 - Supports 3D lossless JPEG saved as [multiple fragments](http://www.nitrc.org/forum/forum.php?thread_id=5872&forum_id=4703).


5-May-2016
 - Crop 3D T1 acquisitions (e.g. ./dcm2niix -x y ~/DICOM).

30-Apr-2016
 - Convert multiple files/folders with single command line invocation (e.g. ./dcm2niix -b y ~/tst ~/tst2).

22-Apr-2016
 - Detect Siemens Phase maps (phase image names end with "_ph").
 - Use current working directory if file name not specified.

12-Apr-2016
 - Provide override (command line option "-m y") to stack images of the same series even if they differ in study date/time, echo/coil number, or slice orientation. This mechanism allows users to concatenate images that break strict DICOM compliance.

22-Mar-2016
 - Experimental support for [DICOM datasets without DICOM file meta information](http://dicom.nema.org/dicom/2013/output/chtml/part10/chapter_7.html).

12-Dec-2015
 - Support PAR/REC FP values when possible (see PMC3998685).

11-Nov-2015
 - Minor refinements.

12-June-2015
 - Uses less memory (helpful for large datasets).

2-Feb-2015
 - Support for Visual Studio.
 - Remove dependency on zlib (now uses miniz).

1-Jan-2015
 - Images separated based on TE (fieldmaps).
 - Support for JPEG2000 using OpenJPEG or Jasper libraries.
 - Support for JPEG using NanoJPEG library.
 - Support for lossless JPEG using custom library.

24-Nov-2014
 - Support for CT scans with gantry tilt and varying distance between slices.

11-Oct-2014
 - Initial public release.

## Running

Command line usage is described in the [NITRC wiki](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#General_Usage). The minimal command line call would be `dcm2niix /path/to/dicom/folder`. However, you may want to invoke additional options, for example the call `dcm2niix -z y -f %p_%t_%s -o /path/ouput /path/to/dicom/folder` will save data as gzip compressed, with the filename based on the protocol name (%p) acquisition time (%t) and DICOM series number (%s), with all files saved to the folder "output". For more help see help: `dcm2niix -h`.

**Optional batch processing version:**

Perform a batch conversion of multiple dicoms using the configurations specified in a yaml file.
```bash
dcm2niibatch batch_config.yml
```

The configuration file should be in yaml format as shown in example `batch_config.yaml`

```yaml
Options:
  isGz:             false
  isFlipY:          false
  isVerbose:        false
  isCreateBIDS:     false
  isOnlySingleFile: false
Files:
    -
      in_dir:           /path/to/first/folder
      out_dir:          /path/to/output/folder
      filename:         dcemri
    -
      in_dir:           /path/to/second/folder
      out_dir:          /path/to/output/folder
      filename:         fa3
```

You can add as many files as you want to convert as long as this structure stays consistent. Note that a dash must separate each file.

## Build

### Build command line version with cmake (Linux, Windows, OSx)

```bash
mkdir build && cd build
cmake ..
make
```
`dcm2niix` will be created in the `bin` subfolder. To install on the system run `make install`.

**optional batch processing version:**

The batch processing binary `dcm2niibatch` is optional. To build `dcm2niibatch` as well change the cmake command to `cmake -DBATCH_VERSION=ON ..`

This requires the following libraries:
- pkg-config
- yaml-cpp
- a compiler that supports c++11

e.g. the dependencies can be installed as follows:

Ubuntu 14.04
```
sudo apt-get install pkg-config libyaml-cpp-dev libyaml-cpp0.5 cmake libboost-dev
```
OSX
```
brew install pkg-config yaml-cpp cmake
```


### Building the command line version without cmake

You can also build the software without C-make. The easiest way to do this is to run the function "make" from the "console" folder. Note that this only creates the default version of dcm2niix, not the optional batch version described above. The make command simply calls the g++ compiler, and if you want you can tune this for your build. In essence, the make function simply calls

```
g++ -dead_strip -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp  -o dcm2niix -DmyDisableOpenJPEG -DmyDisableJasper
```

The following sub-sections list how you can modify this basic recipe for your needs.

##### ZLIB BUILD
 If we have zlib, we can use it (-lz) and disable [miniz](https://code.google.com/p/miniz/) (-myDisableMiniZ)

```
g++ -O3 -DmyDisableOpenJPEG -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp -dead_strip -o dcm2niix -lz -DmyDisableMiniZ
```

##### MINGW BUILD

If you use the (obsolete) compiler MinGW on Windows you will want to include the rare libgcc libraries with your executable so others can use it. Here I also demonstrate the optional "-DmyDisableZLib" to remove zip support.

```
g++ -O3 -s -DmyDisableOpenJPEG -DmyDisableZLib -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp -o dcm2niix  -static-libgcc
```

##### DISABLING CLASSIC JPEG

DICOM images can be stored as either raw data or compressed using one of many formats as described by the [transfer syntaxes](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Transfer_Syntaxes_and_Compressed_Images). One of the compressed formats is the lossy classic JPEG format (which is separate from and predates the lossy JPEG 2000 format). This software comes with the [NanoJPEG](http://keyj.emphy.de/nanojpeg/) library to handle these images. However, you can use the `myDisableClassicJPEG` compiler switch to remove this dependency. The resulting executable will be smaller but will not be able to convert images stored with this format.

```
g++ -dead_strip -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp  -o dcm2niix -DmyDisableClassicJPEG -DmyDisableOpenJPEG -DmyDisableJasper
```

##### USING LIBJPEG-TURBO TO DECODE CLASSIC JPEG

By default, classic JPEG images will be decoded using the [compact NanoJPEG decoder](http://keyj.emphy.de/nanojpeg/). However, the compiler directive `myTurboJPEG`  will create an executable based on the [libjpeg-turbo](http://www.libjpeg-turbo.org) library. This library is a faster decoder and is the standard for many Linux distributions. On the other hand, the lossy classic JPEG is rarely used for DICOM images, so this compilation has extra dependencies and can result in a larger executable size (for static builds).

```
g++ -dead_strip -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp  -o dcm2niix -DmyDisableOpenJPEG -DmyDisableJasper -DmyTurboJPEG -I/opt/libjpeg-turbo/include /opt/libjpeg-turbo/lib/libturbojpeg.a
```

##### JPEG2000 BUILD

 If you want to build this with JPEG2000 decompression support using OpenJPEG. You will need to have the OpenJPEG 2.1 libraries installed (https://code.google.com/p/openjpeg/wiki/Installation). I suggest building static libraries...
 svn checkout http://openjpeg.googlecode.com/svn/trunk/ openjpeg-read-only
 cmake -DBUILD_SHARED_LIBS:bool=off .
 make
 sudo make install
You should then be able to run then run:

```
g++ -O3 -dead_strip -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp  jpg_0XC3.cpp ujpeg.cpp -o dcm2niix -lopenjp2
```

But in my experience this works best if you explicitly tell the software how to find the libraries, so your compile will probably look like one of these two options:

```
g++ -O3 -dead_strip -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp -o dcm2niix  -I/usr/local/include /usr/local/lib/libopenjp2.a
```

```
g++ -O3 -dead_strip -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp -o dcm2niix  -I/usr/local/lib /usr/local/lib/libopenjp2.a
```

 If you want to build this with JPEG2000 decompression support using Jasper: You will need to have the Jasper (http://www.ece.uvic.ca/~frodo/jasper/) and libjpeg (http://www.ijg.org) libraries installed which for Linux users may be as easy as running 'sudo apt-get install libjasper-dev' (otherwise, see http://www.ece.uvic.ca/~frodo/jasper/#doc). You can then run:

```
g++ -O3 -DmyDisableOpenJPEG -DmyEnableJasper -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp  -s -o dcm2niix -ljasper -ljpeg
```

##### VISUAL STUDIO BUILD

This software can be compiled with VisualStudio 2015. This example assumes the compiler is in your path.

```
vcvarsall amd64
cl /EHsc main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp -DmyDisableOpenJPEG -DmyDisableJasper /odcm2niix
```

##### OSX BUILD WITH BOTH 32 AND 64-BIT SUPPORT

Building command line version universal binary from OSX 64 bit system:
 This requires a C compiler. With a terminal, change directory to the 'conosle' folder and run the following:

```
g++ -O3 -DmyDisableOpenJPEG -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp -dead_strip -arch i386 -o dcm2niix32
```

```
g++ -O3 -DmyDisableOpenJPEG -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp -dead_strip -o dcm2niix64
```

```
lipo -create dcm2niix32 dcm2niix64 -o dcm2niix
```

 To validate that the resulting executable supports both architectures type

```
file ./dcm2niix
```

##### OSX GRAPHICAL INTERFACE BUILD

You can building the OSX graphical user interface using Xcode. First, Copy contents of "console" folder to /xcode/dcm2/core. Next, open and compile the project "dcm2.xcodeproj" with Xcode 4.6 or later

##### THE QT AND wxWIDGETS GUIs ARE NOT YET SUPPORTED - FOLLOWING LINES FOR FUTURE VERSIONS

Building QT graphical user interface:
  Open "dcm2.pro" with QTCreator. This should work on OSX and Linux. On Windows the printf information is not redirected to the user interface
  If compile gives you grief look at the .pro file which has notes for different operating systems.

Building using wxWidgets
wxWdigets makefiles are pretty complex and specific for your operating system. For simplicity, we will build the "clipboard" example that comes with wxwidgets and then substitute our own code. The process goes something like this.
 a.) Install wxwdigets
 b.) successfully "make" the samples/clipboard program
 c.) DELETE console/makefile. WE DO NOT WANT TO OVERWRITE the WX MAKEFILE
 d.) with the exception of "makefile", copy the contents of console to /samples/clipboard
 e.) overwrite the original /samples/clipboard.cpp with the dcm2niix file of the same name
 f.) Older Xcodes have problems with .cpp files, whereas wxWidgets's makefiles do not compile with "-x cpp". So the core files are called '.c' but we will rename them to .cpp for wxWidgets:
 rename 's/\.c$/\.cpp/' *
 g.) edit the /samples/clipboard makefile: Add "nii_dicom.o nifti1_io_core.o nii_ortho.o nii_dicom_batch.o \" to CLIPBOARD_OBJECTS:
CLIPBOARD_OBJECTS =  \
  nii_dicom.o nifti1_io_core.o nii_ortho.o nii_dicom_batch.o \
  $(__clipboard___win32rc) \
  $(__clipboard_os2_lib_res) \
  clipboard_clipboard.o
 h.) edit the /samples/clipboard makefile: With wxWidgets we will capture std::cout comments, not printf, so we need to add "-DDmyUseCOut" to CXXFLAGS:
CXXFLAGS = -DmyUseCOut -DWX_PRECOMP ....
 i.) For a full refresh
rm clipboard
rm *.o
make
