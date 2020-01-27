# Check if git exists
find_package(Git)
if(NOT GIT_FOUND)
    message(FATAL_ERROR "Cannot find Git. Git is required for Superbuild")
endif()

# Use git protocol or not
option(USE_GIT_PROTOCOL "If behind a firewall turn this off to use http instead." ON)
if(USE_GIT_PROTOCOL)
    set(git_protocol "git")
else()
    set(git_protocol "https")
endif()

# Basic CMake build settings
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING
        "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS  "Debug;Release;RelWithDebInfo;MinSizeRel")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(USE_STATIC_RUNTIME "Use static runtime" ON)

if(USE_STATIC_RUNTIME)
    if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
        find_file(STATIC_LIBCXX "libstdc++.a" ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})
        mark_as_advanced(STATIC_LIBCXX)
        if(NOT STATIC_LIBCXX)
            unset(STATIC_LIBCXX CACHE)
            # Only on some Centos/Redhat systems
            message(FATAL_ERROR
                "\"USE_STATIC_RUNTIME\" set to ON but \"libstdc++.a\" not found! Set it to OFF or \
                 \"yum install libstdc++-static\" to resolve the error.")
        endif()
    endif()
endif()

option(USE_TURBOJPEG "Use TurboJPEG to decode classic JPEG" OFF)
option(USE_JASPER "Build with JPEG2000 support using Jasper" OFF)
option(USE_OPENJPEG "Build with JPEG2000 support using OpenJPEG" OFF)
option(USE_JPEGLS "Build with JPEG-LS support using CharLS" OFF)

option(BATCH_VERSION "Build dcm2niibatch for multiple conversions" OFF)

include(ExternalProject)

set(DEPENDENCIES)

option(INSTALL_DEPENDENCIES "Optionally install built dependent libraries (OpenJPEG and yaml-cpp) for future use." OFF)

if(INSTALL_DEPENDENCIES)
    set(DEP_INSTALL_DIR ${CMAKE_INSTALL_PREFIX})
else()
    set(DEP_INSTALL_DIR ${CMAKE_BINARY_DIR})
endif()

if(USE_OPENJPEG)
    message("-- Build with OpenJPEG: ${USE_OPENJPEG}")

    if(OpenJPEG_DIR)
        set(OpenJPEG_DIR "${OpenJPEG_DIR}" CACHE PATH "Path to OpenJPEG configuration file"  FORCE)
        message("--   Using OpenJPEG library from ${OpenJPEG_DIR}")
    else()
        find_package(PkgConfig)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(OPENJPEG libopenjp2)
        endif()

        if(OPENJPEG_FOUND AND NOT ${OPENJPEG_INCLUDE_DIRS} MATCHES "gdcmopenjpeg")
            set(OpenJPEG_DIR ${OPENJPEG_LIBDIR}/openjpeg-2.1 CACHE PATH "Path to OpenJPEG configuration file" FORCE)
            message("--   Using OpenJPEG library from ${OpenJPEG_DIR}")
        else()
            if(${OPENJPEG_INCLUDE_DIRS} MATCHES "gdcmopenjpeg")
                message("--   Unable to use GDCM's internal OpenJPEG")
            endif()
            include(${CMAKE_SOURCE_DIR}/SuperBuild/External-OPENJPEG.cmake)
            list(APPEND DEPENDENCIES openjpeg)
            set(BUILD_OPENJPEG TRUE)
            message("--   Will build OpenJPEG library from github")
        endif()
    endif()
endif()

if(BATCH_VERSION)
    message("-- Build dcm2niibatch: ${BATCH_VERSION}")

    if(YAML-CPP_DIR)
        set(YAML-CPP_DIR ${YAML-CPP_DIR} CACHE PATH "Path to yaml-cpp configuration file"  FORCE)
        message("--   Using yaml-cpp library from ${YAML-CPP_DIR}")
    else()
        find_package(PkgConfig)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(YAML-CPP yaml-cpp)
        endif()

        # Build from github if not found or version < 0.5.3
        if(YAML-CPP_FOUND AND NOT (YAML-CPP_VERSION VERSION_LESS "0.5.3"))
            set(YAML-CPP_DIR ${YAML-CPP_LIBDIR}/cmake/yaml-cpp CACHE PATH "Path to yaml-cpp configuration file"  FORCE)
            message("--   Using yaml-cpp library from ${YAML-CPP_DIR}")
        else()
            include(${CMAKE_SOURCE_DIR}/SuperBuild/External-YAML-CPP.cmake)
            list(APPEND DEPENDENCIES yaml-cpp)
            set(BUILD_YAML-CPP TRUE)
            message("--   Will build yaml-cpp library from github")
        endif()
    endif()
endif()

set(ZLIB_IMPLEMENTATION "Miniz" CACHE STRING "Choose zlib implementation.")
set_property(CACHE ZLIB_IMPLEMENTATION PROPERTY STRINGS  "Miniz;System;Cloudflare;Custom")
if(${ZLIB_IMPLEMENTATION} STREQUAL "Cloudflare")
    message("-- Build with Cloudflare zlib: ON")
    include(${CMAKE_SOURCE_DIR}/SuperBuild/External-CLOUDFLARE-ZLIB.cmake)
    list(APPEND DEPENDENCIES zlib)
    set(BUILD_CLOUDFLARE-ZLIB TRUE)
    message("--   Will build Cloudflare zlib from github")
elseif(${ZLIB_IMPLEMENTATION} STREQUAL "Custom")
    set(ZLIB_ROOT ${ZLIB_ROOT} CACHE PATH "Specify custom zlib root directory.")
    if(NOT ZLIB_ROOT)
        message(FATAL_ERROR "ZLIB_ROOT needs to be set to locate custom zlib!")
    endif()
endif()

ExternalProject_Add(console
    DEPENDS ${DEPENDENCIES}
    DOWNLOAD_COMMAND ""
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/console
    BINARY_DIR console-build
    CMAKE_ARGS
        -Wno-dev
        --no-warn-unused-cli
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}
        -DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS}
        -DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS}
        -DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}
        -DUSE_STATIC_RUNTIME:BOOL=${USE_STATIC_RUNTIME}
        -DUSE_TURBOJPEG:BOOL=${USE_TURBOJPEG}
        -DUSE_JASPER:BOOL=${USE_JASPER}
        -DUSE_JPEGLS:BOOL=${USE_JPEGLS}
        -DZLIB_IMPLEMENTATION:STRING=${ZLIB_IMPLEMENTATION}
        -DZLIB_ROOT:PATH=${ZLIB_ROOT}
         # OpenJPEG
        -DUSE_OPENJPEG:BOOL=${USE_OPENJPEG}
        -DOpenJPEG_DIR:PATH=${OpenJPEG_DIR}
        # yaml-cpp
        -DBATCH_VERSION:BOOL=${BATCH_VERSION}
        -DYAML-CPP_DIR:PATH=${YAML-CPP_DIR}
)

install(DIRECTORY ${CMAKE_BINARY_DIR}/bin/ DESTINATION bin
        USE_SOURCE_PERMISSIONS)

option(BUILD_DOCS "Build documentation (manpages)" OFF)
if(BUILD_DOCS)
    add_subdirectory(docs)
endif()

