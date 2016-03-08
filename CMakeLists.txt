project(dcm2niix)
cmake_minimum_required(VERSION 2.6)

# Option1: Choose whether to build the batch version
option(BATCH_VERSION "Build dcm2niibatch for multiple conversions as well" OFF)
message(${BATCH_VERSION})

# Option2: Build qt gui
# TODO

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")

cmake_minimum_required(VERSION 2.6)
#
# Zlib
#
#find_package(ZLIB)

# Resolve system dependency on yaml-cpp, which apparently does not
# provide a CMake find_package() module.
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(YAML_CPP yaml-cpp)
    find_path(YAML_CPP_INCLUDE_DIR
            NAMES yaml_cpp.h
            PATHS ${YAML_CPP_INCLUDE_DIRS})
    find_library(YAML_CPP_LIBRARY
            NAMES YAML_CPP
            PATHS ${YAML_CPP_LIBRARY_DIRS})
endif()

# Predefined permission set to enforce proper permissions
# during install even if files in the sources have different
# settings
set(FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

##
## Sub-projects
##
add_subdirectory(console)
