# Regular use
CFLAGS=-s -O3

# Debugging
#CFLAGS=-g

ifneq ($(OS),Windows_NT)
	OS = $(shell uname)
 	ifeq "$(OS)" "Darwin"
		CFLAGS=-dead_strip -O3
	endif
endif
all:
	g++ $(CFLAGS) -I. main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp  -o dcm2niix -DmyDisableOpenJPEG
