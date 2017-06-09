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

set(PROGRAMS dcm2niix)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    # using Clang
    add_definitions(-Wno-deprecated-register)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-dead_strip")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # using GCC
    add_definitions(-Wno-unused-result)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
    # using Intel C++
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
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
endif()

add_executable(dcm2niix
    main_console.cpp
    nii_dicom.cpp
    jpg_0XC3.cpp
    ujpeg.cpp
    nifti1_io_core.cpp
    nii_foreign.cpp
    nii_ortho.cpp
    nii_dicom_batch.cpp)

option(USE_SYSTEM_ZLIB "Use the system zlib" OFF)
if(USE_SYSTEM_ZLIB)
    find_package(ZLIB REQUIRED)
    add_definitions(-DmyDisableMiniZ)
    target_include_directories(dcm2niix PRIVATE ${ZLIB_INCLUDE_DIRS})
    target_link_libraries(dcm2niix ${ZLIB_LIBRARIES})
endif()

option(USE_SYSTEM_TURBOJPEG "Use the system TurboJPEG" OFF)
if(USE_SYSTEM_TURBOJPEG)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TurboJPEG REQUIRED libturbojpeg)
    add_definitions(-DmyTurboJPEG)
    target_include_directories(dcm2niix PRIVATE ${TurboJPEG_INCLUDE_DIRS})
    target_link_libraries(dcm2niix ${TurboJPEG_LIBRARIES})
endif()

option(USE_JASPER "Compile with Jasper support" OFF)
if(USE_JASPER)
    find_package(Jasper REQUIRED)
    add_definitions(-DmyEnableJasper)
    target_include_directories(dcm2niix PRIVATE ${JASPER_INCLUDE_DIR})
    target_link_libraries(dcm2niix ${JASPER_LIBRARIES})
else ()
    add_definitions(-DmyDisableJASPER)
endif()

option(USE_OPENJPEG "Build with OpenJPEG support" OFF)
if(USE_OPENJPEG)
    option(OpenJPEG_DIR "Path to OpenJPEG configuration file" "")

    find_package(OpenJPEG)

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
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

    add_executable(dcm2niibatch
        main_console_batch.cpp
        nii_dicom.cpp
        jpg_0XC3.cpp
        ujpeg.cpp
        nifti1_io_core.cpp
        nii_foreign.cpp
        nii_ortho.cpp
        nii_dicom_batch.cpp)

    option(YAML-CPP_DIR "Path to yaml-cpp configuration file" "")

    find_package(YAML-CPP)
    target_include_directories(dcm2niibatch PRIVATE ${YAML_CPP_INCLUDE_DIR})
    target_link_libraries(dcm2niibatch ${YAML_CPP_LIBRARIES})

    if(ZLIB_FOUND)
        add_definitions(-DmyDisableMiniZ)
        target_link_libraries(dcm2niibatch z)
    endif()

    if(TurboJPEG_FOUND)
        target_include_directories(dcm2niibatch PRIVATE ${TurboJPEG_INCLUDE_DIRS})
        target_link_libraries(dcm2niibatch ${TurboJPEG_LIBRARIES})
    endif()

    if(JASPER_FOUND)
        target_link_libraries(dcm2niibatch ${JASPER_LIBRARIES})
    else()
        add_definitions(-DmyDisableJASPER)
    endif()

    if(USE_OPENJPEG)
        target_include_directories(dcm2niibatch PRIVATE ${OPENJPEG_INCLUDE_DIRS})
        target_link_libraries(dcm2niibatch ${OPENJPEG_LIBRARIES})
    else()
        add_definitions(-DmyDisableOpenJPEG)
    endif()

    list(APPEND PROGRAMS dcm2niibatch)
endif()

install(TARGETS ${PROGRAMS} DESTINATION bin)
