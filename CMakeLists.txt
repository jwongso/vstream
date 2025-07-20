cmake_minimum_required(VERSION 3.20)
project(vstream VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
cmake_policy(SET CMP0167 NEW)

# Build options
option(BUILD_TESTS "Build test suite" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(BUILD_EXAMPLES "Build example clients" OFF)

# Enable parallel compilation with your 24 threads
set(CMAKE_BUILD_PARALLEL_LEVEL 24)
message(STATUS "Parallel compilation with 24 threads")

# Find dependencies
find_package(hyni_websocket_server REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
find_path(CONCURRENTQUEUE_INCLUDE_DIR
    NAMES moodycamel/concurrentqueue.h
    PATHS /usr/include/concurrentqueue
          /usr/local/include/concurrentqueue
          /usr/include
          /usr/local/include
)
find_path(FVAD_INCLUDE_DIR fvad.h)
find_library(FVAD_LIBRARY fvad)

if(NOT FVAD_INCLUDE_DIR OR NOT FVAD_LIBRARY)
    message(FATAL_ERROR "libfvad not found. Please install it first.")
endif()

if(NOT CONCURRENTQUEUE_INCLUDE_DIR)
    if(EXISTS /etc/fedora-release)
        message(FATAL_ERROR
            "Could not find moodycamel/concurrentqueue.h\n"
            "On Fedora, install it with:\n"
            "  sudo dnf install moodycamel-concurrentqueue-devel\n"
            "Searched in: /usr/include/concurrentqueue")
    else()
        message(FATAL_ERROR
            "Could not find moodycamel/concurrentqueue.h\n"
            "Please install the concurrentqueue development headers.\n"
            "Searched in: /usr/include/concurrentqueue")
    endif()
endif()

# Enable ccache
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    message("Found ccache ${CCACHE_FOUND}")
    message("Using ccache to speed up compilation")
    set(ENV{CCACHE_CPP2} "yes")
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_FOUND})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_FOUND})
endif(CCACHE_FOUND)

# Detect compiler version
execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -dumpversion
    OUTPUT_VARIABLE GCC_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "Compiler version: ${GCC_VERSION}")

# Check if compiler supports znver5
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-march=znver5" COMPILER_SUPPORTS_ZNVER5)

if(COMPILER_SUPPORTS_ZNVER5)
    set(MARCH_FLAG "-march=znver5")
    message(STATUS "Using Zen 5 optimizations (-march=znver5)")
else()
    set(MARCH_FLAG "-march=znver4")
    message(STATUS "Compiler doesn't support znver5, using znver4")
endif()

# Check for mold linker
find_program(MOLD_LINKER mold)
if(MOLD_LINKER)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=mold")
    message(STATUS "Using mold linker for faster linking")
endif()

# Common flags for all configurations
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wno-deprecated-declarations
    -fPIC
)

# Configuration-specific flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(
        -O3
        ${MARCH_FLAG}
        -mtune=znver5
        # AVX-512 support
        -mavx512f
        -mavx512dq
        -mavx512cd
        -mavx512bw
        -mavx512vl
        -mavx512ifma
        -mavx512vbmi
        -mavx512vbmi2
        -mavx512vnni
        -mavx512bitalg
        -mavx512vpopcntdq
        -mavx512bf16
        # Other SIMD
        -mavx2
        -mfma
        -mf16c
        -msha
        -maes
        -mpclmul
        -mrdrnd
        -mrdseed
        # Optimization flags
        -flto=auto
        -fomit-frame-pointer
        -funroll-loops
        -fprefetch-loop-arrays
        -ffast-math
        -fno-math-errno
        -ffinite-math-only
        -fno-trapping-math
        -fassociative-math
        -freciprocal-math
        -ftree-vectorize
        -ftree-loop-vectorize
        -ftree-slp-vectorize
        -mprefer-vector-width=512
        # Alignment
        -falign-functions=32
        -falign-jumps=32
        -falign-loops=32
        -falign-labels=32
        # Other optimizations
        -fno-plt
        -fno-semantic-interposition
        -pipe
        -fstack-protector-strong
    )

    # Compiler-specific optimizations
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(
            -fgraphite-identity
            -floop-nest-optimize
            -ftree-loop-distribution
            -ftree-loop-ivcanon
        )
    endif()

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=auto -s -Wl,--gc-sections")

elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(
        -O0
        -g3
        -ggdb
        -fno-omit-frame-pointer
        -fsanitize=address,undefined
        -fno-optimize-sibling-calls
        -fstack-protector-all
    )
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")

elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_options(
        -O2
        ${MARCH_FLAG}
        -mtune=znver5
        -mavx512f
        -mavx512dq
        -mavx512cd
        -mavx512bw
        -mavx512vl
        -mavx2
        -mfma
        -g
        -fno-omit-frame-pointer
        -flto=auto
    )
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=auto")
endif()

# Enable interprocedural optimization for Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# Find Vosk library
find_library(VOSK_LIBRARY vosk REQUIRED)
find_path(VOSK_INCLUDE_DIR vosk_api.h REQUIRED)

# Create library for easier testing
add_library(vstream_lib STATIC
    src/vstream_engine.cpp
    src/mic_capture.cpp
    src/vad.cpp
    src/audio_processor.cpp
    src/vstream_app.cpp
    src/logger.cpp
    src/benchmark_manager.cpp
)

target_include_directories(vstream_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${VOSK_INCLUDE_DIR}
    ${CONCURRENTQUEUE_INCLUDE_DIR}
    ${PORTAUDIO_INCLUDE_DIRS}
    ${FVAD_INCLUDE_DIR}
)

target_link_libraries(vstream_lib PUBLIC
    hyni_websocket_server::hyni_websocket_server
    ${VOSK_LIBRARY}
    nlohmann_json::nlohmann_json
    Threads::Threads
    ${PORTAUDIO_LIBRARIES}
    ${FVAD_LIBRARY}
)

# Create vstream executable
add_executable(vstream src/main.cpp)

# Enable unity builds for faster compilation
set_target_properties(vstream PROPERTIES
    UNITY_BUILD ON
    UNITY_BUILD_BATCH_SIZE 4
)

# Precompiled headers for faster builds
target_precompile_headers(vstream PRIVATE
    <vector>
    <string>
    <memory>
    <atomic>
    <thread>
    <mutex>
    <chrono>
)

target_link_libraries(vstream PRIVATE vstream_lib)

# Testing
if(BUILD_TESTS)
    enable_testing()

    # Find GTest
    find_package(GTest REQUIRED)

    # Enable coverage if requested
    if(ENABLE_COVERAGE)
        add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    endif()

    # Create test executable
    add_executable(vstream_tests
        tests/test_audio_processor.cpp
        tests/test_mic_capture.cpp
        tests/test_vad.cpp
        tests/test_vstream_engine.cpp
        tests/test_vstream_app.cpp
    )

    target_link_libraries(vstream_tests PRIVATE
        vstream_lib
        GTest::gtest_main
        GTest::gmock
    )

    # Add tests
    include(GoogleTest)
    gtest_discover_tests(vstream_tests)

    # Coverage target
    if(ENABLE_COVERAGE)
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E make_directory coverage
            COMMAND gcovr -r ${CMAKE_SOURCE_DIR} --html --html-details -o coverage/index.html
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating coverage report"
        )
    endif()
endif()

if (BUILD_EXAMPLES)
    add_subdirectory(examples/)
endif()

# Installation
install(TARGETS vstream DESTINATION bin)

# Print configuration summary
message(STATUS "")
message(STATUS "=== vstream Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER} ${GCC_VERSION}")
message(STATUS "Architecture: ${MARCH_FLAG}")
message(STATUS "SIMD Support: AVX-512 (Full), AVX2, FMA, SHA, AES")
message(STATUS "CPU: AMD Ryzen AI 9 HX 370 (Zen 5)")
message(STATUS "Cores/Threads: 12/24")
message(STATUS "Build tests: ${BUILD_TESTS}")
if(BUILD_TESTS AND ENABLE_COVERAGE)
    message(STATUS "Code coverage: ENABLED")
endif()
message(STATUS "===================================")
