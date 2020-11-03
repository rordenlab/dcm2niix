## About

The README.md file describes the typical compilation of the software, using the `make` command to build the software. This document describes advanced methods for compiling and tuning the software.

Beyond the complexity of compiling the software, the only downside to adding optional modules is that the dcm2niix executable size will require a tiny bit more disk space. For example, on MacOS the stripped basic executable is 238kb, miniz (GZip support) adds 18kb, NanoJPEG (lossy JPEG support) adds 13kb, CharLS (JPEG-LS support) adds 271kb, and OpenJPEG (JPEG2000 support) adds 192kb. So with all these features installed the executable weighs in at 732kb.

## Choosing your compiler

The text below generally describes how to build dcm2niix using the [GCC](https://gcc.gnu.org) compiler using the `g++` command. However, the code is portable and you can use different compilers. For [clang/llvm](https://clang.llvm.org) compile using `clang++`.  If you have the [Intel C compiler](https://software.intel.com/en-us/c-compilers), you can substitute the `icc` command. The code is compatible with Microsoft's VS 2015 or later. For [Microsoft's C compiler](http://landinghub.visualstudio.com/visual-cpp-build-tools) you would use the `cl` command. In theory, the code should support other compilers, but this has not been tested. Be aware that if you do not have gcc installed the `g++` command may use a default to a compiler (e.g. clang). To check what compiler was used, run the dcm2niix software: it always reports the version and the compiler used for the build.

## Building the command line version without cmake

You can also build the software without C-make. The easiest way to do this is to run the function "make" from the "console" folder. Note that this only creates the default version of dcm2niix, not the optional batch version described above. The make command simply calls the g++ compiler, and if you want you can tune this for your build. In essence, the make function simply calls

```
g++ -dead_strip -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG
```

The following sub-sections list how you can modify this basic recipe for your needs.

## Trouble Shooting

Some [Centos/Redhat](https://github.com/rordenlab/dcm2niix/issues/137) may report "/usr/bin/ld: cannot find -lstdc++". This can be resolved by installing static versions of libstdc++:  `yum install libstdc++-static`.

To compile with debugging symbols, use
```
cmake -DUSE_OPENJPEG=ON -DCMAKE_CXX_FLAGS=-g .. && make
```

##### ZLIB BUILD
 If we have zlib, we can use it (-lz) and disable [miniz](https://code.google.com/p/miniz/) (-myDisableMiniZ)

```
g++ -O3 -DmyDisableOpenJPEG -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -dead_strip -o dcm2niix -lz -DmyDisableMiniZ
```

##### MINGW BUILD

If you use the (obsolete) compiler MinGW on Windows you will want to include the rare libgcc libraries with your executable so others can use it. Here I also demonstrate the optional "-DmyDisableZLib" to remove zip support.

```
g++ -O3 -s -DmyDisableOpenJPEG -DmyDisableZLib -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -o dcm2niix -static-libgcc
```

##### DISABLING CLASSIC JPEG

DICOM images can be stored as either raw data or compressed using one of many formats as described by the [transfer syntaxes](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Transfer_Syntaxes_and_Compressed_Images). One of the compressed formats is the lossy classic JPEG format (which is separate from and predates the lossy JPEG 2000 format). This software comes with the [NanoJPEG](http://keyj.emphy.de/nanojpeg/) library to handle these images. However, you can use the `myDisableClassicJPEG` compiler switch to remove this dependency. The resulting executable will be smaller but will not be able to convert images stored with this format.

```
g++ -dead_strip -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp nii_foreign.cpp -o dcm2niix -DmyDisableClassicJPEG -DmyDisableOpenJPEG
```

##### USING LIBJPEG-TURBO TO DECODE CLASSIC JPEG

By default, classic JPEG images will be decoded using the [compact NanoJPEG decoder](http://keyj.emphy.de/nanojpeg/). However, the compiler directive `myTurboJPEG`  will create an executable based on the [libjpeg-turbo](http://www.libjpeg-turbo.org) library. This library is a faster decoder and is the standard for many Linux distributions. On the other hand, the lossy classic JPEG is rarely used for DICOM images, so this compilation has extra dependencies and can result in a larger executable size (for static builds).

```
g++ -dead_strip -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -DmyTurboJPEG -I/opt/libjpeg-turbo/include /opt/libjpeg-turbo/lib/libturbojpeg.a
```

##### JPEG-LS BUILD

You can compile dcm2niix to convert DICOM images compressed with the [JPEG-LS](https://en.wikipedia.org/wiki/JPEG_2000) [transfer syntaxes 1.2.840.10008.1.2.4.80 and 1.2.840.10008.1.2.4.81](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Transfer_Syntaxes_and_Compressed_Images). Decoding this format is handled by the [CharLS library](https://github.com/team-charls/charls), which is included with dcm2niix in the `charls` folder. The included code was downloaded from the CharLS website on 6 June 2018. To enable support you will need to include the `myEnableJPEGLS` compiler flag as well as a few file sin the `charls` folder. Therefore, a minimal compile (with just JPEG-LS and without JPEG2000) should look like this:

`g++ -I. -DmyEnableJPEGLS  charls/jpegls.cpp charls/jpegmarkersegment.cpp charls/interface.cpp  charls/jpegstreamwriter.cpp charls/jpegstreamreader.cpp main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp  -o dcm2niix -DmyDisableOpenJPEG`

Alternatively, you can decompress an image in JPEG-LS to an uncompressed DICOM using [gdcmconv](https://github.com/malaterre/GDCM)(e.g. `gdcmconv -w 3691459 3691459.dcm`). Or you can use gdcmconv compress a DICOM to JPEG-LS (e.g. `gdcmconv -L 3691459 3691459.dcm`). Alternatively, the DCMTK tool [dcmcjpls](https://support.dcmtk.org/docs/dcmcjpls.html) provides JPEG-LS support.



##### JPEG2000 BUILD

You can compile dcm2niix to convert DICOM images compressed with the [JPEG2000](https://en.wikipedia.org/wiki/JPEG_2000) [transfer syntaxes 1.2.840.10008.1.2.4.90 and 1.2.840.10008.1.2.4.91](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Transfer_Syntaxes_and_Compressed_Images). This is optional, as JPEG2000 is very rare in DICOMs (usually only created by the proprietary DCMJP2K or OsiriX). Due to the challenges discussed below this is a poor choice for archiving DICOM data. Rather than support conversion with dcm2niix, a better solution would be to use DCMJP2K to do a DICOM-to-DICOM conversion to a more widely supported transfer syntax. Unfortunately, JPEG2000 saw poor adoption as a general image format. This situation is unlikely to change, as JPEG2000 only offered incremental benefits over the simpler classic JPEG, and is outperformed by the more recent [HEIF](https://en.wikipedia.org/wiki/High_Efficiency_Image_File_Format). This has implications for DICOM, as there is little active development on libraries to decode JPEG2000. Indeed, the two popular open-source libraries that decode JPEG2000 have serious limitations for processing these images. Some JPEG2000 DICOM images can not be decoded by the default compilation of OpenJPEG library after version [2.1.0](https://github.com/uclouvain/openjpeg/issues/962). On the other hand, the Jasper library does not handle lossy [16-bit](https://en.wikipedia.org/wiki/JPEG_2000) images with good precision.

You can build dcm2niix with JPEG2000 decompression support using OpenJPEG 2.1.0. You will need to have the OpenJPEG library installed (use the package manager of your Linux distribution, Homebrew for macOS, or see [here](https://github.com/uclouvain/openjpeg/blob/master/INSTALL.md) if you want to build it yourself). If you want to use a more recent version of OpenJPEG, it must be custom-compiled with `-DOPJ_DISABLE_TPSOT_FIX` compiler flag. I suggest building static libraries where you would [download the code](https://github.com/uclouvain/openjpeg) and run
```
 cmake -DBUILD_SHARED_LIBS:bool=off -DOPJ_DISABLE_TPSOT_FIX:bool=on .
 make
 sudo make install
```
You should then be able to run:

```
g++ -O3 -dead_strip -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -o dcm2niix -lopenjp2
```

But in my experience this works best if you explicitly tell the software how to find the libraries, so your compile will probably look like one of these options:

```
#for MacOS
g++ -O3 -dead_strip -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -o dcm2niix -I/usr/local/include/openjpeg-2.1 /usr/local/lib/libopenjp2.a
```
```
#For older Linux
g++ -O3 -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -o dcm2niix -I/usr/local/lib /usr/local/lib/libopenjp2.a
```
```
#For modern Linux
g++ -O3 -s -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -lpthread -o dcm2niix -I/usr/local/include/openjpeg-2.2 ~/openjpeg-master/build/bin/libopenjp2.a
```

 If you want to build this with JPEG2000 decompression support using Jasper: You will need to have the Jasper (http://www.ece.uvic.ca/~frodo/jasper/) and libjpeg (http://www.ijg.org) libraries installed which for Linux users may be as easy as running 'sudo apt-get install libjasper-dev' (otherwise, see http://www.ece.uvic.ca/~frodo/jasper/#doc). You can then run:

```
g++ -O3 -DmyDisableOpenJPEG -DmyEnableJasper -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -s -o dcm2niix -ljasper -ljpeg
```

##### VISUAL STUDIO BUILD

This software can be compiled with VisualStudio 2015. This example assumes the compiler is in your path.

```
vcvarsall amd64
cl /EHsc main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp nii_foreign.cpp -DmyDisableOpenJPEG /o dcm2niix
```

##### OSX BUILD WITH BOTH 32 AND 64-BIT SUPPORT

Building command line version universal binary from OSX 64 bit system:
 This requires a C compiler. With a terminal, change directory to the 'conosle' folder and run the following:

```
g++ -O3 -DmyDisableOpenJPEG -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -dead_strip -arch i386 -o dcm2niix32
```

```
g++ -O3 -DmyDisableOpenJPEG -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -dead_strip -o dcm2niix64
```

```
lipo -create dcm2niix32 dcm2niix64 -o dcm2niix
```

 To validate that the resulting executable supports both architectures type

```
file ./dcm2niix
```

##### CMAKE INSTALLATION

While most of this page describes  how to use `make` to compile dcm2niix, `cmake` can automatically aid complex builds. The [home page](https://github.com/rordenlab/dcm2niix) describes typical cmake options. The cmake command will attempt to pull additional code from git as needed for zlib, OpenJPEG etc.  If you get the following error:

```
fatal: unable to connect to github.com:
github.com[0: 140.82.121.4]: errno=Connection timed out
```

This suggests git is unable to connect using ssh. You have two options, first you can disable the cmake option USE_GIT_PROTOCOL (which is on by default). Alternatively, to use https instead using the following lines prior to running cmake:

```
git config --global url."https://github.com/".insteadOf git@github.com:
git config --global url."https://".insteadOf git://
```

Once the installation is completed, you can revert these changes:

```
git config --global --unset-all url.https://github.com/.insteadof
git config --global --unset-all url.https://.insteadof
```

