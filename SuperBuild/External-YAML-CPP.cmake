set(YAML-CPP_TAG yaml-cpp-0.5.3) # version yaml-cpp-0.5.3

ExternalProject_Add(yaml-cpp
    GIT_REPOSITORY "https://github.com/ningfei/yaml-cpp.git"
    GIT_TAG "${YAML-CPP_TAG}"
    SOURCE_DIR yaml-cpp
    BINARY_DIR yaml-cpp-build
    CMAKE_ARGS
        -Wno-dev
        --no-warn-unused-cli
        ${EXTERNAL_PROJECT_BUILD_TYPE_CMAKE_ARGS}
        ${OSX_ARCHITECTURES}
        # Compiler settings
        -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
        # Install directories
        -DCMAKE_INSTALL_PREFIX:PATH=${DEP_INSTALL_DIR}
)

set(YAML-CPP_DIR ${DEP_INSTALL_DIR}/lib/cmake/yaml-cpp)
