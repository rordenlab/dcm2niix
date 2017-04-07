set(YAML-CPP_TAG 5c3cb09) # version yaml-cpp-0.5.3

ExternalProject_Add(yaml-cpp
    GIT_REPOSITORY "${git_protocol}://github.com/ningfei/yaml-cpp.git"
    GIT_TAG "${YAML-CPP_TAG}"
    SOURCE_DIR yaml-cpp
    BINARY_DIR yaml-cpp-build
    CMAKE_ARGS
        -Wno-dev
        --no-warn-unused-cli
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}
)

set(YAML-CPP_DIR ${CMAKE_BINARY_DIR}/lib/cmake/yaml-cpp)
