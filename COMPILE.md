## About

The [README.md file] describes the typical compilation of the software, using the `cmake` command. This document introduces advanced methods for compiling and tuning the software.

The design of dcm2niix is fairly modular. In particular, it can be built to use different libraries to handle the decompression and compression of files. It can even be built without these dependencies, resulting in compact software that will be unable to handle compressed images. Beyond the complexity of compiling the software, the only downside to adding optional modules is that the dcm2niix executable size will require a tiny bit more disk space. For example, miniz (GZip support) adds 18kb, NanoJPEG (lossy JPEG support) adds 13kb, CharLS (JPEG-LS support) adds 271kb, and OpenJPEG (JPEG2000 support) adds 192kb.

The first two sections describe using `make` and `cmake` to build dcm2niix. The subsequent sections provide in depth notes on compiling directly from the command line. Beware that those notes can get outdated as compilers and options evolve. Further, they can make explicit assumptions regarding the location of libraries. You can always run the `make` script to see the current typical compile for your system.

##### MAKE INSTALLATION

To download and compile dcm2niix with make you can run these commands from the command line (remove `--branch development` to get the current stable release):

```bash
git clone --branch development git@github.com:rordenlab/dcm2niix.git
cd dcm2niix\console
make
```

The make file supports different build configurations. 

| command         | conifguratio             |
| --------------- | ------------------------ |
| make            |                          |
| make debug      | unoptimized code         |
| make jp2        | JP2000 support           |
| make noroi      | Ignore [overlays](https://dicom.nema.org/dicom/2013/output/chtml/part03/sect_C.9.html#:~:text=A%20Region%20of%20Interest%20(ROI,the%20image%20of%20particular%20interest.)|
| make sanitize   | [memory error detector.](https://clang.llvm.org/docs/AddressSanitizer.html) |
| make wasm       | [WebAssembly](https://github.com/rordenlab/dcm2niix/tree/master/js)|.

You can also append prefix(es) to each of these configurations, for example `JPEGLS=1 ZLIB=1 make jp2` will use the system zlib, CharLS and OpenJPEG:

| prefix          | features                 |
| --------------- | ------------------------ |
| JPEGLS=1        | Statically add CharLS library |
| ZLIB=1          | Dynamically link to system zlib instead of static miniz |
| JNIfTI=0        | compile without [jnifti](https://github.com/NeuroJSON/jnifti) support |
##### CMAKE INSTALLATION

`cmake` can automatically aid complex builds. The [home page](https://github.com/rordenlab/dcm2niix) describes typical cmake options.

To download and compile dcm2niix with cmake you can run these commands from the command line (remove `--branch development` to get the current stable release; remove `-DZLIB_IMPLEMENTATION=Cloudflare` to produce with the miniz compressor, remove `-DUSE_JPEGLS=ON` to build without CharLS and remove `-DUSE_OPENJPEG=ON` to compile without JPEG2000 support):

```bash
git clone --branch development git@github.com:rordenlab/dcm2niix.git
cd dcm2niix
mkdir build && cd build
cmake -DZLIB_IMPLEMENTATION=Cloudflare -DUSE_JPEGLS=ON -DUSE_OPENJPEG=ON ..
make
```

If you get the following error:

```
fatal: unable to connect to github.com:
github.com[0: 140.82.121.4]: errno=Connection timed out
```

This suggests git is unable to connect using ssh. One solution is to use https instead:

```
git clone --branch development https://github.com/rordenlab/dcm2niix.git
```

## Choosing your compiler

The text below generally describes how to build dcm2niix using the [GCC](https://gcc.gnu.org) compiler using the `g++` command. However, the code is portable and you can use different compilers. For [clang/llvm](https://clang.llvm.org) compile using `clang++`.  If you have the [Intel C compiler](https://software.intel.com/en-us/c-compilers), you can substitute the `icc` command. The code is compatible with Microsoft's VS 2015 or later. For [Microsoft's C compiler](http://landinghub.visualstudio.com/visual-cpp-build-tools) you would use the `cl` command. In theory, the code should support other compilers, but this has not been tested. Be aware that if you do not have gcc installed the `g++` command may use a default to a compiler (e.g. clang). To check what compiler was used, run the dcm2niix software: it always reports the version and the compiler used for the build.

Note that in the commands below we increase the [stack size](https://stackoverflow.com/questions/18909395/how-do-i-increase-the-stack-size-when-compiling-with-clang-on-os-x)zgit to 16mb, which is larger than the Unix (8mb) and Windows (1mb) defaults.

## Trouble Shooting

Some [Centos/Redhat](https://github.com/rordenlab/dcm2niix/issues/137) may report "/usr/bin/ld: cannot find -lstdc++". This can be resolved by installing static versions of libstdc++:  `yum install libstdc++-static`.

To compile with debugging symbols, use
```
cmake -DUSE_OPENJPEG=ON -DCMAKE_CXX_FLAGS=-g .. && make
```

##### ZLIB BUILD
 If we have zlib, we can use it (-lz) and disable [miniz](https://code.google.com/p/miniz/) (-DmyDisableMiniZ)

```
g++ -O3 -DmyDisableOpenJPEG -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -o dcm2niix -lz -DmyDisableMiniZ g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -Wl,-stack_size -Wl,3f00000
```

##### MINGW BUILD

If you use the (obsolete) compiler MinGW on Windows you will want to include the rare libgcc libraries with your executable so others can use it. Here I also demonstrate the optional "-DmyDisableZLib" to remove zip support.

```
g++ -O3 -s -DmyDisableOpenJPEG -DmyDisableZLib -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -o dcm2niix -static-libgcc g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -Wl,-stack_size -Wl,3f00000
```

##### DISABLING CLASSIC JPEG

DICOM images can be stored as either raw data or compressed using one of many formats as described by the [transfer syntaxes](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Transfer_Syntaxes_and_Compressed_Images). One of the compressed formats is the lossy classic JPEG format (which is separate from and predates the JPEG2000 and JPEG-LS formats). This software comes with the [NanoJPEG](http://keyj.emphy.de/nanojpeg/) library to handle these images. However, you can use the `myDisableClassicJPEG` compiler switch to remove this dependency. The resulting executable will be smaller but will not be able to convert images stored with this format.

```
g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableClassicJPEG -DmyDisableOpenJPEG g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -Wl,-stack_size -Wl,3f00000
```

##### USING LIBJPEG-TURBO TO DECODE CLASSIC JPEG

By default, classic JPEG images will be decoded using the [compact NanoJPEG decoder](http://keyj.emphy.de/nanojpeg/). However, the compiler directive `myTurboJPEG`  will create an executable based on the [libjpeg-turbo](http://www.libjpeg-turbo.org) library. This library is a faster decoder and is the standard for many Linux distributions. On the other hand, the lossy classic JPEG is rarely used for DICOM images, so this compilation has extra dependencies and can result in a larger executable size (for static builds).

```
g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -DmyTurboJPEG -I/opt/libjpeg-turbo/include /opt/libjpeg-turbo/lib/libturbojpeg.a g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -Wl,-stack_size -Wl,3f00000
```

##### JPEG-LS BUILD

You can compile dcm2niix to convert DICOM images compressed with the [JPEG-LS](https://en.wikipedia.org/wiki/JPEG_2000) [transfer syntaxes 1.2.840.10008.1.2.4.80 and 1.2.840.10008.1.2.4.81](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Transfer_Syntaxes_and_Compressed_Images). Decoding this format is handled by the [CharLS library](https://github.com/team-charls/charls), which is included with dcm2niix in the `charls` folder. The included code was downloaded from the CharLS website on 6 June 2018. To enable support you will need to include the `myEnableJPEGLS` compiler flag as well as a few file sin the `charls` folder. Therefore, a minimal compile (with just JPEG-LS and without JPEG2000) should look like this:

```
g++ -I. -DmyEnableJPEGLS  charls/jpegls.cpp charls/jpegmarkersegment.cpp charls/interface.cpp  charls/jpegstreamwriter.cpp charls/jpegstreamreader.cpp main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp  -o dcm2niix -DmyDisableOpenJPEG g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -Wl,-stack_size -Wl,3f00000
```

Alternatively, you can decompress an image in JPEG-LS to an uncompressed DICOM using [gdcmconv](https://github.com/malaterre/GDCM) (e.g. `gdcmconv -w 3691459 3691459.dcm`). Or you can use gdcmconv compress a DICOM to JPEG-LS (e.g. `gdcmconv -L 3691459 3691459.dcm`). Alternatively, the DCMTK tool [dcmcjpls](https://support.dcmtk.org/docs/dcmcjpls.html) provides JPEG-LS support.

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
g++ -O3 -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -o dcm2niix -lopenjp2 g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -Wl,-stack_size -Wl,3f00000
```

But in my experience this works best if you explicitly tell the software how to find the libraries, so your compile will probably look like one of these options:

```
#for MacOS
g++ -O3 -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -o dcm2niix -I/usr/local/include/openjpeg-2.1 /usr/local/lib/libopenjp2.a g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -Wl,-stack_size -Wl,3f00000
#For older Linux
g++ -O3 -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -o dcm2niix -I/usr/local/lib /usr/local/lib/libopenjp2.a g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -Wl,-stack_size -Wl,3f00000
#For modern Linux
g++ -O3 -s -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -lpthread -o dcm2niix -I/usr/local/include/openjpeg-2.2 ~/openjpeg-master/build/bin/libopenjp2.a g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -Wl,-stack_size -Wl,3f00000
```

 If you want to build this with JPEG2000 decompression support using Jasper: You will need to have the Jasper (http://www.ece.uvic.ca/~frodo/jasper/) and libjpeg (http://www.ijg.org) libraries installed which for Linux users may be as easy as running 'sudo apt-get install libjasper-dev' (otherwise, see http://www.ece.uvic.ca/~frodo/jasper/#doc). You can then run:

```
g++ -O3 -DmyDisableOpenJPEG -DmyEnableJasper -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -s -o dcm2niix -ljasper -ljpeg g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -Wl,-stack_size -Wl,3f00000
```

##### VISUAL STUDIO BUILD

This software can be compiled with [Microsoft's Visual Studio C compiler](http://landinghub.visualstudio.com/visual-cpp-build-tools). This example assumes the compiler is in your path (For Windows 11 you can run the `x64 Native Tools Command Prompt`).

Crucially, you will want to [set a large stack allocation](https://learn.microsoft.com/en-us/cpp/build/reference/stack-stack-allocations?view=msvc-170). This allows dcm2niix to convert a huge number of DICOM images in a single pass (which requires a large amount of memory).

```
cl /wd4018 /wd4068 /wd4101 /wd4244 /wd4267 /wd4305 /wd4308 /wd4334 /wd4800 /wd4819 /wd4996  base64.cpp cJSON.cpp  main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp /Fe:dcm2niix.exe -DmyDisableOpenJPEG /link /STACK:8388608
```

##### MacOS BUILD UNIVERSAl BINARIES SUPPORT

On MacOS you can create Universal binaries, that bundle optimized code for different architectures. For example, supporting PowerPC, Intel and Apple Silicon (e.g. M1) CPUs. Further, you can optimize Intel code for either 32-bit or 64-bit operation. More details on Universal binaries and notarization is provided  [here](https://github.com/neurolabusc/NotarizeC).  

Here is a simple example of creating independent 32-bit and 64-bit executables and then using `lipo` to create a single universal executable:

```
g++ -O3 -DmyDisableOpenJPEG -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -arch i386 -o dcm2niix32 g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -Wl,-stack_size -Wl,3f00000
g++ -O3 -DmyDisableOpenJPEG -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp jpg_0XC3.cpp ujpeg.cpp nii_foreign.cpp -o dcm2niix64 g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp reproin.cpp nii_foreign.cpp -o dcm2niix -DmyDisableOpenJPEG -Wl,-stack_size -Wl,3f00000
lipo -create dcm2niix32 dcm2niix64 -o dcm2niix
```

 To validate that the resulting executable supports both architectures type

```
file ./dcm2niix
```

