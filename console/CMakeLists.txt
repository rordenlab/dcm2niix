project(console)
set(PROGRAMS dcm2niix)

add_executable(dcm2niix
  main_console.cpp
  nii_dicom.cpp
  nifti1_io_core.cpp
  nii_ortho.cpp
  nii_dicom_batch.cpp)
#We now use miniz, removing zlib dependency
#if(ZLIB_FOUND)
#  TARGET_LINK_LIBRARIES(dcm2niix z)
#else(ZLIB_FOUND)
#  ADD_DEFINITIONS(-DmyDisableZlib)
endif(ZLIB_FOUND)
ADD_DEFINITIONS(-DmyDisableJasper)

install(TARGETS ${PROGRAMS} DESTINATION bin)
