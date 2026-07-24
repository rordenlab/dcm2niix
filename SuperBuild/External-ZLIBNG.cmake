set(ZLIBNG_TAG 2.3.3) # zlib-ng release: actively maintained, x86 + ARM SIMD

# The ExternalProject is named `zlib` (matching the Cloudflare/Custom paths) so
# the rest of SuperBuild.cmake (DEPENDENCIES, ZLIB_ROOT) is implementation-
# agnostic. ZLIB_COMPAT=ON makes zlib-ng install the classic zlib API as
# libz / zlib.h, so the inner build's find_package(ZLIB) locates it unchanged.
# BUILD_SHARED_LIBS=OFF yields a static libz for self-contained binaries/wheels.
ExternalProject_Add(zlib
    GIT_REPOSITORY "https://github.com/zlib-ng/zlib-ng.git"
    GIT_TAG "${ZLIBNG_TAG}"
    SOURCE_DIR zlib-ng
    BINARY_DIR zlib-ng-build
    CMAKE_ARGS
        -Wno-dev
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
        -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
        # Compiler settings
        -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
        # zlib-ng options
        -DZLIB_COMPAT:BOOL=ON
        -DZLIB_ENABLE_TESTS:BOOL=OFF
        -DWITH_GTEST:BOOL=OFF
        -DBUILD_SHARED_LIBS:BOOL=OFF
        # Install directories
        -DCMAKE_INSTALL_PREFIX:PATH=${DEP_INSTALL_DIR}
)

set(ZLIB_ROOT ${DEP_INSTALL_DIR})
