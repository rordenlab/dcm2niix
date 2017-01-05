project(dcm2niix)
cmake_minimum_required(VERSION 2.8)

include(ucm.cmake)

# Option1: Choose whether to build the batch version
option(BATCH_VERSION "Build dcm2niibatch for multiple conversions as well" OFF)
message(${BATCH_VERSION})

# Option2: Choose whether to use static runtime
option(USE_STATIC_RUNTIME "Use static runtime" ON)
if(USE_STATIC_RUNTIME)
    ucm_set_runtime(STATIC)
else()
    ucm_set_runtime(DYNAMIC)
endif()

# Option3: Choose build type
set(CMAKE_BUILD_TYPE "Release" CACHE STRING
    "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")

#
# Zlib
#
#find_package(ZLIB)

# Predefined permission set to enforce proper permissions
# during install even if files in the sources have different
# settings
set(FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

##
## Sub-projects
##
add_subdirectory(console)

option(BUILD_DOCS "Build documentation (manpages)" OFF)
if (BUILD_DOCS)
    add_subdirectory(docs)
endif ()
