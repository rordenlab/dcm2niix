# This module allows to consistently manage the initialization and setting of build type
# CMake variables.
#
# It sets the variable EXTERNAL_PROJECT_BUILD_TYPE_CMAKE_ARGS based on the CMake generator
# being used:
# * If a multi-config generator (e.g Visual Studio) is used, it sets the variable with
#   CMAKE_CONFIGURATION_TYPES.
# * If a single-config generator (e.g Unix Makefiles) is used, it sets the variable with
#   CMAKE_BUILD_TYPE.
#
# Adapted from https://github.com/Slicer/Slicer/blob/5.2/CMake/SlicerInitializeBuildType.cmake

# Default build type to use if none was specified
if(NOT DEFINED dcm2niix_DEFAULT_BUILD_TYPE)
    set(dcm2niix_DEFAULT_BUILD_TYPE "Release")
endif()

# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)

    message(STATUS "Setting build type to '${dcm2niix_DEFAULT_BUILD_TYPE}' as none was specified.")

    set(CMAKE_BUILD_TYPE ${dcm2niix_DEFAULT_BUILD_TYPE} CACHE STRING "Choose the type of build." FORCE)
    mark_as_advanced(CMAKE_BUILD_TYPE)

    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug"
        "Release"
        "MinSizeRel"
        "RelWithDebInfo"
        )
endif()

# Pass variables to dependent projects
if(COMMAND ExternalProject_Add)
    if(NOT CMAKE_CONFIGURATION_TYPES)
        set(EXTERNAL_PROJECT_BUILD_TYPE_CMAKE_ARGS
            -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
            )
    else()
        set(EXTERNAL_PROJECT_BUILD_TYPE_CMAKE_ARGS
            -DCMAKE_CONFIGURATION_TYPES:STRING=${CMAKE_CONFIGURATION_TYPES}
            )
    endif()
endif()

