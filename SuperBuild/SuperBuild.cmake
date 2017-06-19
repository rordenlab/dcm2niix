# Check if git exists
find_package(Git)
if(NOT GIT_FOUND)
    message(ERROR "Cannot find git. git is required for Superbuild")
endif()

# Use git protocol or not
option(USE_GIT_PROTOCOL "If behind a firewall turn this off to use http instead." ON)
if(USE_GIT_PROTOCOL)
    set(git_protocol "git")
else()
    set(git_protocol "https")
endif()

# Basic CMake build settings
set(CMAKE_BUILD_TYPE "Release" CACHE STRING
    "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS  "Debug;Release;RelWithDebInfo;MinSizeRel")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(USE_STATIC_RUNTIME "Use static runtime" ON)

option(USE_SYSTEM_ZLIB "Use the system zlib" OFF)
option(USE_SYSTEM_TURBOJPEG "Use the system TurboJPEG" OFF)
option(USE_JASPER "Compile with Jasper support" OFF)

option(USE_OPENJPEG "Build with OpenJPEG support" OFF)
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

    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(OPENJPEG libopenjp2)
    endif()

    if(OPENJPEG_FOUND)
        set(OPENJPEG_DIR ${OPENJPEG_LIBDIR}/openjepg-2.1)
        message("--     Using the system OpenJPEG")
    else()
        include(${CMAKE_SOURCE_DIR}/SuperBuild/External-OPENJPEG.cmake)
        list(APPEND DEPENDENCIES openjpeg)
        set(BUILD_OPENJPEG TRUE)
        message("--     Will build OpenJPEG from github")
    endif()
endif()

if(BATCH_VERSION)
    message("-- Build dcm2niibatch: ${BATCH_VERSION}")

    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(YAML-CPP yaml-cpp)
    endif()

    if(YAML-CPP_FOUND)
        set(YAML-CPP_DIR ${YAML-CPP_LIBDIR}/cmake/yaml-cpp)
        message("--     Using the system yaml-cpp")
    else()
        include(${CMAKE_SOURCE_DIR}/SuperBuild/External-YAML-CPP.cmake)
        list(APPEND DEPENDENCIES yaml-cpp)
        set(BUILD_YAML-CPP TRUE)
        message("--     Will build yaml-cpp from github")
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
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}
        -DUSE_STATIC_RUNTIME:BOOL=${USE_STATIC_RUNTIME}
        -DUSE_SYSTEM_ZLIB:BOOL=${USE_SYSTEM_ZLIB}
        -DUSE_SYSTEM_TURBOJPEG:BOOL=${USE_SYSTEM_TURBOJPEG}
        -DUSE_SYSTEM_JASPER:BOOL=${USE_SYSTEM_JASPER}
         # OpenJPEG
        -DUSE_OPENJPEG:BOOL=${USE_OPENJPEG}
        -DOpenJPEG_DIR:PATH=${OPENJPEG_DIR}
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

