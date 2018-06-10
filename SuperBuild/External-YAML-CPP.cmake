set(YAML-CPP_TAG yaml-cpp-0.5.3) # version yaml-cpp-0.5.3

ExternalProject_Add(yaml-cpp
    GIT_REPOSITORY "${git_protocol}://github.com/ningfei/yaml-cpp.git"
    GIT_TAG "${YAML-CPP_TAG}"
    SOURCE_DIR yaml-cpp
    BINARY_DIR yaml-cpp-build
    CMAKE_ARGS
        -Wno-dev
        --no-warn-unused-cli
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX:PATH=${DEP_INSTALL_DIR}
)

set(YAML-CPP_DIR ${DEP_INSTALL_DIR}/lib/cmake/yaml-cpp)
