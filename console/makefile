# Regular use
CFLAGS=-s -O3

# Debugging
#CFLAGS=-g

# issue659: increase stack to 16mb
#For Linux linker stacksize ignored: we must use setrlimit 
# LFLAGS=-Wl,-z -Wl,stack-size=16777216
LFLAGS=


#Leak tests:
# https://clang.llvm.org/docs/AddressSanitizer.html
# clang++ -O1 -g -fsanitize=address -fno-omit-frame-pointer -I.  main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp base64.c cJSON.c -o dcm2niix -DmyDisableOpenJPEG


#run "make" for default build
#run "JPEGLS=1 make" for JPEGLS build
JFLAGS=
ifeq "$(JPEGLS)" "1"
	JFLAGS=-std=c++14 -DmyEnableJPEGLS  charls/jpegls.cpp charls/jpegmarkersegment.cpp charls/interface.cpp  charls/jpegstreamwriter.cpp charls/jpegstreamreader.cpp
endif

#run "JNIfTI=0 make" to disable JNIFTI build
JSFLAGS=
ifneq "$(JNIfTI)" "0"
	JSFLAGS=-DmyEnableJNIfTI base64.cpp cJSON.cpp
endif

ifneq ($(OS),Windows_NT)
	OS = $(shell uname)
 	ifeq "$(OS)" "Darwin"
		#CFLAGS=-dead_strip -O3
		#CFLAGS= -O3
		CFLAGS=-O3 -sectcreate __TEXT __info_plist Info.plist
		#Apple notarization requires a Info.plist
		# For .app bundles, the Info.plist is a separate file, for executables it is appended as a section
		#you can check that the Info.plist section has been inserted with either of these commands
		# otool -l ./dcm2niix | grep info_plist -B1 -A10
		# launchctl plist ./dcm2niix
		#MacOS links g++ to clang++, for gcc install via homebrew and replace g++ with /usr/local/bin/gcc-9
		LFLAGS=-Wl,-stack_size -Wl,0x1000000
	endif
endif
all:
	g++ $(CFLAGS) -I. $(JSFLAGS) $(JFLAGS) $(LFLAGS) main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp -o dcm2niix -DmyDisableOpenJPEG
