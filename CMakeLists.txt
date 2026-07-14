cmake_minimum_required(VERSION 3.15)

project(chardsp LANGUAGES CXX)

include(CTest)
option(CHARDSP_BUILD_EXAMPLES "Build the chardsp examples" ON)

set(CHARDSP_CHOC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/choc")
set(CHARDSP_ELLIPTIC_BLEP_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/external/elliptic-blep")

if (NOT EXISTS "${CHARDSP_CHOC_DIR}/choc/audio/choc_SampleBuffers.h"
    OR NOT EXISTS "${CHARDSP_ELLIPTIC_BLEP_DIR}/elliptic-blep.h")
    message(FATAL_ERROR
        "Required chardsp submodules are missing. Run:\n"
        "  git submodule update --init --recursive")
endif()

add_library(chardsp INTERFACE)
target_include_directories(chardsp INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CHARDSP_CHOC_DIR}"
    "${CHARDSP_ELLIPTIC_BLEP_DIR}")
target_compile_features(chardsp INTERFACE cxx_std_17)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()

if (CHARDSP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
