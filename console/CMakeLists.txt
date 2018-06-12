cmake_minimum_required(VERSION 2.8.11)

project(console)

# Option Choose whether to use static runtime
include(ucm.cmake)
option(USE_STATIC_RUNTIME "Use static runtime" ON)
if(USE_STATIC_RUNTIME)
    ucm_set_runtime(STATIC)
else()
    ucm_set_runtime(DYNAMIC)
endif()

# Basic CMake build settings
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING
        "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS  "Debug;Release;RelWithDebInfo;MinSizeRel")
endif()

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    # using Clang
    add_definitions(-fno-caret-diagnostics)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-dead_strip")
elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
    # using GCC
    if(NOT (${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS 4.5.0))
        add_definitions(-Wno-unused-result)    # available since GCC 4.5.0
    endif()
    if(NOT (${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS 4.8.0))
        add_definitions(-fno-diagnostics-show-caret)    # available since GCC 4.8.0
    endif()
elseif(MSVC)
    # using Visual Studio C++
    add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4018")   # '<': signed/unsigned mismatch
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4068")   # unknown pragma
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4101")   # unreferenced local variable
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4244")   # 'initializing': conversion from 'double' to 'int', possible loss of data
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4267")   # 'initializing': conversion from 'size_t' to 'int', possible loss of data
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4305")   # 'argument': truncation from 'double' to 'float'
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4308")   # negative integral constant converted to unsigned type
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4334")   # '<<': result of 32-bit shift implicitly converted to 64 bits
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4800")   # 'uint32_t' : forcing value to bool 'true' or 'false'
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4819")   # The file contains a character that cannot be represented in the current code page
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996")   # 'access': The POSIX name for this item is deprecated
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:8388608")  # set "Stack Reserve Size" to 8MB (default value is 1MB)
endif()

# Compiler dependent flags
include (CheckCXXCompilerFlag)
if(UNIX)
    check_cxx_compiler_flag(-msse2 HAS_SSE2)
    if(HAS_SSE2)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2 -mfpmath=sse")
    endif()
endif()

set(PROGRAMS dcm2niix)

set(DCM2NIIX_SRCS
    main_console.cpp
    nii_dicom.cpp
    jpg_0XC3.cpp
    ujpeg.cpp
    nifti1_io_core.cpp
    nii_foreign.cpp
    nii_ortho.cpp
    nii_dicom_batch.cpp)

option(USE_JPEGLS "Build with JPEG-LS support using CharLS" OFF)
if(USE_JPEGLS)
    add_definitions(-DmyEnableJPEGLS)
    if(MSVC)
        add_definitions(-DCHARLS_STATIC)
    endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

    set(CHARLS_SRCS
        charls/jpegls.cpp
        charls/jpegmarkersegment.cpp
        charls/interface.cpp
        charls/jpegstreamwriter.cpp
        charls/jpegstreamreader.cpp)
    add_executable(dcm2niix ${DCM2NIIX_SRCS} ${CHARLS_SRCS})
else()
    add_executable(dcm2niix ${DCM2NIIX_SRCS})
endif()

set(ZLIB_IMPLEMENTATION "Miniz" CACHE STRING "Choose zlib implementation.")
set_property(CACHE ZLIB_IMPLEMENTATION PROPERTY STRINGS  "Miniz;System;Custom")
if(NOT ${ZLIB_IMPLEMENTATION} STREQUAL "Miniz")
    if(NOT ${ZLIB_IMPLEMENTATION} STREQUAL "System")
        set(ZLIB_ROOT ${ZLIB_ROOT} CACHE PATH "Specify custom zlib root directory.")
        if(NOT ZLIB_ROOT)
            message(FATAL_ERROR "ZLIB_ROOT needs to be set to locate custom zlib!")
        endif()
    endif()
    find_package(ZLIB REQUIRED)
    add_definitions(-DmyDisableMiniZ)
    target_include_directories(dcm2niix PRIVATE ${ZLIB_INCLUDE_DIRS})
    target_link_libraries(dcm2niix ${ZLIB_LIBRARIES})
endif()

option(USE_TURBOJPEG "Use TurboJPEG to decode classic JPEG" OFF)
if(USE_TURBOJPEG)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TURBOJPEG REQUIRED libturbojpeg)
    add_definitions(-DmyTurboJPEG)
    target_include_directories(dcm2niix PRIVATE ${TURBOJPEG_INCLUDEDIR})
    target_link_libraries(dcm2niix ${TURBOJPEG_LIBRARIES})
endif()

option(USE_JASPER "Build with JPEG2000 support using Jasper" OFF)
if(USE_JASPER)
    find_package(Jasper REQUIRED)
    add_definitions(-DmyEnableJasper)
    target_include_directories(dcm2niix PRIVATE ${JASPER_INCLUDE_DIR})
    target_link_libraries(dcm2niix ${JASPER_LIBRARIES})
endif()

option(USE_OPENJPEG "Build with JPEG2000 support using OpenJPEG" OFF)
if(USE_OPENJPEG)
    set(OpenJPEG_DIR "${OpenJPEG_DIR}" CACHE PATH "Path to OpenJPEG configuration file" FORCE)

    find_package(OpenJPEG REQUIRED)

    if(WIN32)
        if(BUILD_SHARED_LIBS)
            add_definitions(-DOPJ_EXPORTS)
        else()
            add_definitions(-DOPJ_STATIC)
        endif()
    endif()

    target_include_directories(dcm2niix PRIVATE ${OPENJPEG_INCLUDE_DIRS})
    target_link_libraries(dcm2niix ${OPENJPEG_LIBRARIES})
else ()
    add_definitions(-DmyDisableOpenJPEG)
endif()

option(BATCH_VERSION "Build dcm2niibatch for multiple conversions" OFF)
if(BATCH_VERSION)
    set(DCM2NIIBATCH_SRCS
        main_console_batch.cpp
        nii_dicom.cpp
        jpg_0XC3.cpp
        ujpeg.cpp
        nifti1_io_core.cpp
        nii_foreign.cpp
        nii_ortho.cpp
        nii_dicom_batch.cpp)

    if(USE_JPEGLS)
        add_executable(dcm2niibatch ${DCM2NIIBATCH_SRCS} ${CHARLS_SRCS})
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
        add_executable(dcm2niibatch ${DCM2NIIBATCH_SRCS})
    endif()

    set(YAML-CPP_DIR ${YAML-CPP_DIR} CACHE PATH "Path to yaml-cpp configuration file" FORCE)

    find_package(YAML-CPP REQUIRED)
    target_include_directories(dcm2niibatch PRIVATE ${YAML_CPP_INCLUDE_DIR})
    target_link_libraries(dcm2niibatch ${YAML_CPP_LIBRARIES})

    if(ZLIB_FOUND)
        target_include_directories(dcm2niibatch PRIVATE ${ZLIB_INCLUDE_DIRS})
        target_link_libraries(dcm2niibatch ${ZLIB_LIBRARIES})
    endif()

    if(TURBOJPEG_FOUND)
        target_include_directories(dcm2niibatch PRIVATE ${TURBOJPEG_INCLUDEDIR})
        target_link_libraries(dcm2niibatch ${TURBOJPEG_LIBRARIES})
    endif()

    if(JASPER_FOUND)
        target_include_directories(dcm2niibatch PRIVATE ${JASPER_INCLUDE_DIR})
        target_link_libraries(dcm2niibatch ${JASPER_LIBRARIES})
    endif()

    if(OPENJPEG_FOUND)
        target_include_directories(dcm2niibatch PRIVATE ${OPENJPEG_INCLUDE_DIRS})
        target_link_libraries(dcm2niibatch ${OPENJPEG_LIBRARIES})
    endif()

    list(APPEND PROGRAMS dcm2niibatch)
endif()

install(TARGETS ${PROGRAMS} DESTINATION bin)
