
Versions

1-Jan-2015
 - Images separated based on TE (fieldmaps)
 - Support for JPEG2000 using OpenJPEG or Jasper libraries
 - Support for JPEG using NanoJPEG library
 - Support for lossless JPEG using custom library

24-Nov-2014
 - Support for CT scans with gantry tilt and varying distance between slices
11-Oct-2014
 - Initial public release

Building command line version:

 This requires a C compiler. With a terminal, change directory to the 'conosle' folder and run the following: 

   g++ -O3 -DmyDisableOpenJPEG -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp -dead_strip -o dcm2niix -lz

If you use the (osbsolete) MinGW on Windows

g++ -O3 -s -DmyDisableOpenJPEG -DmyDisableZLib -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp -o dcm2niix  -static-libgcc


Or  for MinGW users who  "mingw-get install libz-dev"
 g++ -O3 -s -DmyDisableOpenJPEG  -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp -o dcm2niix  -static-libgcc -lz



if you do not have zlib,you can compile without it by defining "myDisableZLib ":

  g++ -O3 -dead_strip -DmyDisableOpenJPEG -DmyDisableZLib -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp  -dead_strip -o dcm2niix 


 If you want to build this with JPEG2000 decompression support using OpenJPEG. You will need to have the OpenJPEG 2.1 libraries installed (https://code.google.com/p/openjpeg/wiki/Installation). I suggest building static libraries...
 svn checkout http://openjpeg.googlecode.com/svn/trunk/ openjpeg-read-only
 cmake -DBUILD_SHARED_LIBS:bool=off .
 make
 sudo make install
You should then be able to run then run:

   g++ -O3 -dead_strip -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp  jpg_0XC3.cpp ujpeg.cpp -o dcm2niix -lz -lopenjp2 
   
But in my experience this works best if you explicitly tell the software how to find the libraries, so your compile will probably look like one of these two options:    
   
 g++ -O3 -dead_strip -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp -o dcm2niix -lz  -I/usr/local/include /usr/local/lib/libopenjp2.a
   
  g++ -O3 -dead_strip -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp -o dcm2niix -lz  -I/usr/local/lib /usr/local/lib/libopenjp2.a
 
 If you want to build this with JPEG2000 decompression support using Jasper: You will need to have the Jasper (http://www.ece.uvic.ca/~frodo/jasper/) and libjpeg (http://www.ijg.org) libraries installed which for Linux users may be as easy as running 'sudo apt-get install libjasper-dev' (otherwise, see http://www.ece.uvic.ca/~frodo/jasper/#doc). You can then run:

  g++ -O3 -DmyDisableOpenJPEG -DmyEnableJasper -I. main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp  -s -o dcm2niix -lz -ljasper -ljpeg
   
Building command line version universal binary from OSX 64 bit system:
 This requires a C compiler. With a terminal, change directory to the 'conosle' folder and run the following: 

  g++ -O3  -DmyDisableJasper -lz main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp  jpg_0XC3.cpp ujpeg.cpp -s -o dcm2niix -s -arch i386 -o dcm2niix32

  g++ -O3  -DmyDisableJasper -lz main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp jpg_0XC3.cpp ujpeg.cpp -s -o dcm2niix -s -o dcm2niix64

  lipo -create dcm2niix32 dcm2niix64 -o dcm2niix

 To validate that the resulting executable supports both architectures type

  file ./dcm2niix

Building OSX graphical user interface using XCode:
 Copy contents of "console" folder to /xcode/dcm2/core
 Open and compile "dcm2.xcodeproj" with XCode 4.6 or later
 
##### THE QT AND wxWIDGETS GUIs ARE NOT YET SUPPORT - FOLLOWING LINES FOR FUTURE VERSIONS ##### 
 
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
 f.) Older XCodes have problems with .cpp files, whereas wxWidgets's makefiles do not compile with "-x cpp". So the core files are called '.c' but we will rename them to .cpp for wxWidgets:
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