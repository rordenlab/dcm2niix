project(dcm2niix)

cmake_minimum_required(VERSION 2.6)
#
# Zlib
#
find_package(ZLIB)

# Predefined permission set to enforce proper permissions
# during install even if files in the sources have different
# settings
set(FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

##
## Sub-projects
##
add_subdirectory(console)
