project(console)
set(PROGRAMS dcm2niix)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  # using Clang
  add_definitions(-dead_strip)
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
#endif(ZLIB_FOUND)
add_definitions(-DmyDisableJasper)
add_definitions(-DmyDisableOpenJPEG)

add_executable(dcm2niix
        main_console.cpp
        nii_dicom.cpp
        jpg_0XC3.cpp
        ujpeg.cpp
        nifti1_io_core.cpp
        nii_ortho.cpp
        nii_dicom_batch.cpp)


if (BATCH_VERSION)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

    message("Creating batch version")

    add_executable(dcm2niibatch
            main_console_batch.cpp
            nii_dicom.cpp
            jpg_0XC3.cpp
            ujpeg.cpp
            nifti1_io_core.cpp
            nii_ortho.cpp
            nii_dicom_batch.cpp)

   target_link_libraries(dcm2niibatch yaml-cpp)
endif()

install(TARGETS ${PROGRAMS} DESTINATION bin)
