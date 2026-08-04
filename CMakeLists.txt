cmake_minimum_required(VERSION 3.15)

project(chardsp LANGUAGES CXX)

set(CHARDSP_TOP_LEVEL OFF)
if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(CHARDSP_TOP_LEVEL ON)
endif()

option(CHARDSP_BUILD_TESTS "Build the chardsp tests" ${CHARDSP_TOP_LEVEL})
option(CHARDSP_BUILD_EXAMPLES "Build the chardsp examples" ${CHARDSP_TOP_LEVEL})

set(CHARDSP_CHOC_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/external/choc"
    CACHE PATH "Path to CHOC")
set(CHARDSP_ELLIPTIC_BLEP_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/external/elliptic-blep"
    CACHE PATH "Path to elliptic-blep")
set(CHARDSP_SIGNALSMITH_DSP_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/external/signalsmith-dsp"
    CACHE PATH "Path to Signalsmith DSP")

if (NOT EXISTS "${CHARDSP_CHOC_DIR}/choc/audio/choc_SampleBuffers.h"
    OR NOT EXISTS "${CHARDSP_ELLIPTIC_BLEP_DIR}/elliptic-blep.h"
    OR NOT EXISTS
       "${CHARDSP_SIGNALSMITH_DSP_DIR}/include/signalsmith-dsp/rates.h")
    message(FATAL_ERROR
        "Required chardsp submodules are missing. Run:\n"
        "  git submodule update --init --recursive")
endif()

add_library(chardsp INTERFACE)
target_include_directories(chardsp INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CHARDSP_CHOC_DIR}"
    "${CHARDSP_ELLIPTIC_BLEP_DIR}"
    "${CHARDSP_SIGNALSMITH_DSP_DIR}/include")
target_compile_features(chardsp INTERFACE cxx_std_17)
add_library(chardsp::chardsp ALIAS chardsp)

if (CHARDSP_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if (CHARDSP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
