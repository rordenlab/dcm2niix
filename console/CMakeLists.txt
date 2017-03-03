cmake_minimum_required(VERSION 2.8)
project(console)
set(PROGRAMS dcm2niix)

if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    # using Clang
    add_definitions(-dead_strip)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # using GCC
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
    # using Intel C++
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    # using Visual Studio C++
endif ()

add_executable(dcm2niix
        main_console.cpp
        nii_dicom.cpp
        jpg_0XC3.cpp
        ujpeg.cpp
        nifti1_io_core.cpp
        nii_ortho.cpp
        nii_dicom_batch.cpp)

option(USE_SYSTEM_ZLIB "Use the system zlib" OFF)
if (USE_SYSTEM_ZLIB)
  find_package(ZLIB REQUIRED)
  add_definitions(-DmyDisableMiniZ)
  target_include_directories(dcm2niix PRIVATE ${ZLIB_INCLUDE_DIRS})
  target_link_libraries(dcm2niix ${ZLIB_LIBRARIES})
endif ()

option(USE_SYSTEM_TURBOJPEG "Use the system TurboJPEG" OFF)
if (USE_SYSTEM_TURBOJPEG)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(TurboJPEG libturbojpeg)
  add_definitions(-DmyTurboJPEG)
  target_include_directories(dcm2niix PRIVATE ${TurboJPEG_INCLUDE_DIRS})
  target_link_libraries(dcm2niix ${TurboJPEG_LIBRARIES})
endif ()

option(USE_OPENJPEG "Compile with OpenJPEG support" OFF)
if (USE_OPENJPEG)
  find_package(OpenJPEG REQUIRED)
  add_definitions(-DmyEnableOpenJPEG)
  target_include_directories(dcm2niix PRIVATE ${OPENJPEG_INCLUDE_DIR})
  target_link_libraries(dcm2niix ${OPENJPEG_LIBRARIES})
else ()
  add_definitions(-DmyDisableOpenJPEG)
endif ()

option(USE_JASPER "Compile with Jasper support" OFF)
if (USE_JASPER)
  find_package(Jasper REQUIRED)
  add_definitions(-DmyEnableJasper)
  target_include_directories(dcm2niix PRIVATE ${JASPER_INCLUDE_DIR})
  target_link_libraries(dcm2niix ${JASPER_LIBRARIES})
else ()
  add_definitions(-DmyDisableJASPER)
endif ()

if (BATCH_VERSION)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

    message("Creating batch version")

    # Resolve system dependency on yaml-cpp, which apparently does not
    # provide a CMake find_package() module.

    # Step 1: Search for library using pkg-config
    find_package(PkgConfig)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(YAML_CPP yaml-cpp)
        find_path(YAML_CPP_INCLUDE_DIR
                NAMES yaml-cpp
                PATHS ${YAML_CPP_INCLUDE_DIRS})
        find_library(YAML_CPP_LIBRARY
                NAMES yaml-cpp
                PATHS ${YAML_CPP_LIBRARY_DIRS})
    endif ()

    # Step 2: Search for library locally. Only searches for a folder called yaml-cpp
    if (NOT YAML_CPP_LIBRARY)
        message("Checking for yaml-cpp local folder: yaml-cpp")
        if (EXISTS ${CMAKE_SOURCE_DIR}/yaml-cpp)
            message("local version found")
            SET(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Build test packages off")
            add_subdirectory(${CMAKE_SOURCE_DIR}/yaml-cpp ${CMAKE_SOURCE_DIR}/yaml-cpp)
            set(YAML_CPP_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/yaml-cpp/include)
        else()
            message("Local version not found")
        endif()
    endif ()

    add_executable(dcm2niibatch
            main_console_batch.cpp
            nii_dicom.cpp
            jpg_0XC3.cpp
            ujpeg.cpp
            nifti1_io_core.cpp
            nii_ortho.cpp
            nii_dicom_batch.cpp)
	if(ZLIB_FOUND)
		ADD_DEFINITIONS(-DmyDisableMiniZ)
  		TARGET_LINK_LIBRARIES(dcm2niibatch z)
	endif(ZLIB_FOUND)
    if(JASPER_FOUND)
        TARGET_LINK_LIBRARIES(dcm2niibatch ${JASPER_LIBRARIES})
    else(JASPER_FOUND)
        ADD_DEFINITIONS(-DmyDisableJASPER)
    endif(JASPER_FOUND)
    if(OPENJPEG_FOUND)
        TARGET_LINK_LIBRARIES(dcm2niibatch ${OPENJPEG_LIBRARIES})
    else(OPENJPEG_FOUND)
        ADD_DEFINITIONS(-DmyDisableOpenJPEG)
    endif(OPENJPEG_FOUND)
    if (YAML_CPP_LIBRARY)
        target_link_libraries(dcm2niibatch ${YAML_CPP_LIBRARY})
    else()
        target_link_libraries(dcm2niibatch yaml-cpp)
    endif()


    if (YAML_CPP_INCLUDE_DIR)
        include_directories(${YAML_CPP_INCLUDE_DIR})
    endif ()
    
    list(APPEND PROGRAMS dcm2niibatch)
endif ()

install(TARGETS ${PROGRAMS} DESTINATION bin)
