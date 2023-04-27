set(OPENJPEG_TAG  v2.1-static) # version v2.1-static

ExternalProject_Add(openjpeg
    GIT_REPOSITORY "https://github.com/ningfei/openjpeg.git"
    GIT_TAG "${OPENJPEG_TAG}"
    SOURCE_DIR openjpeg
    BINARY_DIR openjpeg-build
    CMAKE_ARGS
        -Wno-dev
        --no-warn-unused-cli
        ${EXTERNAL_PROJECT_BUILD_TYPE_CMAKE_ARGS}
        ${OSX_ARCHITECTURES}
        # Compiler settings
        -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
        # Install directories
        -DCMAKE_INSTALL_PREFIX:PATH=${DEP_INSTALL_DIR}
)

set(OpenJPEG_DIR ${DEP_INSTALL_DIR}/lib/openjpeg-2.1)
