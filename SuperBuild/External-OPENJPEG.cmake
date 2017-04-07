set(OPENJPEG_TAG 151e322) # version openjepg-2.1

ExternalProject_Add(openjpeg
    GIT_REPOSITORY "${git_protocol}://github.com/ningfei/openjpeg.git"
    GIT_TAG "${OPENJPEG_TAG}"
    SOURCE_DIR openjpeg
    BINARY_DIR openjpeg-build
    CMAKE_ARGS
        -Wno-dev
        --no-warn-unused-cli
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}
)

set(OPENJPEG_DIR ${CMAKE_BINARY_DIR}/lib/openjpeg-2.1)
