cl /wd4018 /wd4068 /wd4101 /wd4244 /wd4267 /wd4305 /wd4308 /wd4334 /wd4800 /wd4819 /wd4996  base64.cpp cJSON.cpp  main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp /Fe:dcm2niix.exe -DmyDisableOpenJPEG /link /STACK:16388608
rm *.exp
rm *.lib
rm *.obj