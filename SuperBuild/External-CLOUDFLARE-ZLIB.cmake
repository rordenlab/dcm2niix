set(CLOUDFLARE_BRANCH gcc.amd64) # Cloudflare zlib branch

ExternalProject_Add(zlib
    GIT_REPOSITORY "https://github.com/ningfei/zlib.git"
    GIT_TAG "${CLOUDFLARE_BRANCH}"
    SOURCE_DIR cloudflare-zlib
    BINARY_DIR cloudflare-zlib-build
    CMAKE_ARGS
        -Wno-dev
        ${EXTERNAL_PROJECT_BUILD_TYPE_CMAKE_ARGS}
        ${OSX_ARCHITECTURES}
        # Compiler settings
        -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
        # Install directories
        -DCMAKE_INSTALL_PREFIX:PATH=${DEP_INSTALL_DIR}
)

set(ZLIB_ROOT ${DEP_INSTALL_DIR})
