# Copyright 2016, The dcm2niix contributors
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the new BSD license. See the license.txt file or visit
# https://opensource.org/licenses/BSD-3-Clause for details.

find_program(SPHINX_EXECUTABLE
        NAMES sphinx-build
        HINTS $ENV{SPHINX_DIR}
        PATH_SUFFIXES bin
        DOC "Sphinx documentation generator")

if (NOT SPHINX_EXECUTABLE)
        message(FATAL_ERROR "sphinx-build executable not found")
endif ()

set(SPHINX_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source")
set(SPHINX_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/build")
set(SPHINX_CACHE_DIR "${CMAKE_CURRENT_BINARY_DIR}/doctrees")
set(SPHINX_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/man")

configure_file("${SPHINX_SOURCE_DIR}/conf.py"
        "${SPHINX_BUILD_DIR}/conf.py"
        COPYONLY)

add_custom_target(build-man ALL
        COMMAND ${SPHINX_EXECUTABLE}
                -b man
                -c "${SPHINX_BUILD_DIR}"
                -d "${SPHINX_CACHE_DIR}"
                "${SPHINX_SOURCE_DIR}"
                "${SPHINX_OUTPUT_DIR}"
        COMMENT "Generating man pages with Sphinx")

install(DIRECTORY "${SPHINX_OUTPUT_DIR}/"
        DESTINATION "share/man/man1")
