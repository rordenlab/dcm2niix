# - Try to find NIFTI
# Once done this will define
#  NIFTI_FOUND - System has NIFTI
#  NIFTI_INCLUDE_DIRS - The NIFTI include directories
#  NIFTI_LIBRARIES - The libraries needed to use NIFTI
#  NIFTI_DEFINITIONS - Compiler switches required for using NIFTI

find_path(NIFTI_INCLUDE_DIR nifti1.h nifti1_io.h
          PATH_SUFFIXES nifti)

find_library(NIFTI_LIBRARY NAMES niftiio)

set(NIFTI_LIBRARIES ${NIFTI_LIBRARY})
set(NIFTI_INCLUDE_DIRS ${NIFTI_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NIFTI DEFAULT_MSG
                                  NIFTI_LIBRARY NIFTI_INCLUDE_DIR)

mark_as_advanced(NIFTI_INCLUDE_DIR NIFTI_LIBRARY)
