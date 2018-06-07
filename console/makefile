# Regular use
CFLAGS=-s -O3

# Debugging
#CFLAGS=-g

#run "make" for default build
#run "JPEGLS=1 make" for JPEGLS build
JFLAGS=
ifeq "$(JPEGLS)" "1"
		JFLAGS=-std=c++14 -DmyEnableJPEGLS  charls/jpegls.cpp charls/jpegmarkersegment.cpp charls/interface.cpp  charls/jpegstreamwriter.cpp charls/jpegstreamreader.cpp
endif

ifneq ($(OS),Windows_NT)
	OS = $(shell uname)
 	ifeq "$(OS)" "Darwin"
		CFLAGS=-dead_strip -O3
	endif
endif
all:
	g++ $(CFLAGS) -I. $(JFLAGS) main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp  -o dcm2niix -DmyDisableOpenJPEG
