1.) wxWdigets makefiles are pretty complex and specific for your operating system. For simplicity, install wxWidgets and make the /samples/clipboard, then replace the original clipboard.cpp with our new program 


2.) Older XCodes have problems with .cpp files, whereas wxWidgets's makefiles do not compile with "-x cpp". So the core files are called '.c' but we will rename them to .cpp for wxWidgets:

 rename 's/\.c$/\.cpp/' *

For WX widgets it usually helps to "make" the original clipboard project on the computer and then edit the makefile:

3.) Add "nii_dicom.o nifti1_io_core.o nii_ortho.o nii_dicom_batch.o \" to CLIPBOARD_OBJECTS:
CLIPBOARD_OBJECTS =  \
	nii_dicom.o nifti1_io_core.o nii_ortho.o nii_dicom_batch.o \
	$(__clipboard___win32rc) \
	$(__clipboard_os2_lib_res) \
	clipboard_clipboard.o
	
4.)  With wxWidgets we will capture std::cout comments, not printf, so we need to add "-DDmyUseCOut" to CXXFLAGS:
CXXFLAGS = -DmyUseCOut -DWX_PRECOMP ....

5.) For a full refresh
rm clipboard
rm *.o
make

