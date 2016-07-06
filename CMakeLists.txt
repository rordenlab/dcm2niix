project(dcm2niix)
cmake_minimum_required(VERSION 2.6)

# Option1: Choose whether to build the batch version
option(BATCH_VERSION "Build dcm2niibatch for multiple conversions as well" OFF)
message(${BATCH_VERSION})

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
