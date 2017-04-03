## About

The README.md file describes the typical compilation of the software, using the `make` command to build the software. This document describes advanced methods for compiling and tuning the software.

## Choosing your compiler

The text below generally describes how to build dcm2niix using the [GCC](https://gcc.gnu.org) compiler using the `g++` command. However, the code is portable and you can use different compilers. For [clang/llvm](https://clang.llvm.org) compile using `clang++`.  If you have the [Intel C compiler](https://software.intel.com/en-us/c-compilers), you can substitute the `icc` command. For [Microsoft's C compiler](http://landinghub.visualstudio.com/visual-cpp-build-tools) you would use the `cl` command. In theory, the code should support other compilers, but this has not been tested. Be aware that if you do not have gcc installed the `g++` command may use a default to a compiler (e.g. clang). To check what compiler was used, run the dcm2niix software: it always reports the version and the compiler used for the build.


## Building the command line version without cmake

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

 If you want to build this with JPEG2000 decompression support using OpenJPEG. You will need to have the OpenJPEG 2.1 libraries installed (https://code.google.com/p/openjpeg/wiki/Installation). I suggest building static libraries where you would [download the code](https://github.com/uclouvain/openjpeg) and run
```
 cmake -DBUILD_SHARED_LIBS:bool=off .
 make
 sudo make install
```
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
