project(dcm2niix)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

find_package(ZLIB)
find_package(NIFTI)
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(OpenJPEG2 libopenjp2)
endif()

# Predefined permission set to enforce proper permissions
# during install even if files in the sources have different
# settings
set(FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

##
## Sub-projects
##
add_subdirectory(console)
