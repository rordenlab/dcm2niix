project(console)
set(PROGRAMS dcm2niix)

add_executable(dcm2niix
  main_console.cpp
  nii_dicom.cpp
  jpg_0XC3.cpp
  ujpeg.cpp
  nifti1_io_core.cpp
  nii_ortho.cpp
  nii_dicom_batch.cpp)
  
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  # using Clang
  ADD_DEFINITIONS(-dead_strip)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  # using GCC
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
  # using Intel C++
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  # using Visual Studio C++
endif()

  
#We now use miniz, removing zlib dependency
#if(ZLIB_FOUND)
#  TARGET_LINK_LIBRARIES(dcm2niix z)
#else(ZLIB_FOUND)
#  ADD_DEFINITIONS(-DmyDisableZlib)
endif(ZLIB_FOUND)
ADD_DEFINITIONS(-DmyDisableJasper)
ADD_DEFINITIONS(-DmyDisableOpenJPEG)

install(TARGETS ${PROGRAMS} DESTINATION bin)
