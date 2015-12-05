project(console)
set(PROGRAMS dcm2niix)

CONFIGURE_FILE(config.h.in config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(dcm2niix
  main_console.cpp
  nii_dicom.cpp
  jpg_0XC3.cpp
  ujpeg.cpp
  nifti1_io_core.cpp
  nifti1_io_ext.cpp
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
if(ZLIB_FOUND)
  ADD_DEFINITIONS(-DmyDisableMiniZ)
  TARGET_LINK_LIBRARIES(dcm2niix ${ZLIB_LIBRARIES})
endif(ZLIB_FOUND)
if(NIFTI_FOUND)
  include_directories(${NIFTI_INCLUDE_DIRS})
  target_link_libraries(dcm2niix ${NIFTI_LIBRARIES})
else()
  include_directories(nifti)
endif()
if(OpenJPEG2_FOUND)
  include_directories(${OpenJPEG2_INCLUDE_DIRS})
  target_link_libraries(dcm2niix ${OpenJPEG2_LIBRARIES})
else()
  ADD_DEFINITIONS(-DmyDisableOpenJPEG)
endif()
ADD_DEFINITIONS(-DmyDisableJasper)

install(TARGETS ${PROGRAMS} DESTINATION bin)
