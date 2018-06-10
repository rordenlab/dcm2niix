set(OPENJPEG_TAG  v2.1-static) # version v2.1-static

ExternalProject_Add(openjpeg
    GIT_REPOSITORY "${git_protocol}://github.com/ningfei/openjpeg.git"
    GIT_TAG "${OPENJPEG_TAG}"
    SOURCE_DIR openjpeg
    BINARY_DIR openjpeg-build
    CMAKE_ARGS
        -Wno-dev
        --no-warn-unused-cli
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX:PATH=${DEP_INSTALL_DIR}
)

set(OpenJPEG_DIR ${DEP_INSTALL_DIR}/lib/openjpeg-2.1)
