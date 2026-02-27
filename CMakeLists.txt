# ==============================================================================
# AstraDB - High-Performance Redis-Compatible Database
# ==============================================================================
# This is a complete rewrite designed to exceed DragonflyDB performance Target:
# 2x DragonflyDB performance, 50% less memory usage
# ==============================================================================

cmake_minimum_required(VERSION 3.20)
project(
  AstraDB
  VERSION 1.0.0
  LANGUAGES CXX)

# ==============================================================================
# Load .env File
# ==============================================================================

function(read_env_file)
  set(env_file "${CMAKE_CURRENT_SOURCE_DIR}/.env")

  if(EXISTS ${env_file})
    message(STATUS "Loading .env file: ${env_file}")

    file(STRINGS ${env_file} env_lines REGEX "^[^#].*=.*")

    foreach(line ${env_lines})
      # Skip empty lines
      if(NOT line MATCHES "^\\s*$")
        # Extract variable name and value
        string(REGEX MATCH "^([^=]+)=(.*)$" _match ${line})
        if(CMAKE_MATCH_1 AND CMAKE_MATCH_2)
          set(var_name ${CMAKE_MATCH_1})
          set(var_value ${CMAKE_MATCH_2})

          # Strip whitespace
          string(STRIP ${var_name} var_name)
          string(STRIP ${var_value} var_value)

          # Remove quotes if present
          string(REPLACE "\"" "" var_value ${var_value})

          # Set the variable
          set(${var_name}
              ${var_value}
              PARENT_SCOPE)

          # Also set it as environment variable for child processes
          set(ENV{${var_name}} ${var_value})

          message(STATUS "  ${var_name}=${var_value}")
        endif()
      endif()
    endforeach()
  else()
    message(STATUS ".env file not found, using system defaults")
  endif()
endfunction()

# Call the function to load .env file
read_env_file()

set(CMH_CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.cmakehub/cache")
# ==============================================================================
# CMakeHub Integration
# ==============================================================================
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/hub/loader.cmake)

# ==============================================================================
# Build Optimization Modules
# ==============================================================================

# C++ Standard Configuration
# Note: We set C++17 manually below to avoid compatibility issues with fmt/spdlog/benchmark
# cmakehub_use(cpp_standards) - Disabled to use custom C++17 setting

# Precompiled Headers (PCH) and Unity Build - Reduces compilation time by 50-70%
# Note: Temporarily disabled due to CMake version compatibility issues Will use
# precompiled_header module instead cmakehub_use(cotire)
#cmakehub_use(precompiled_header)

# Link Time Optimization - Improves runtime performance
#cmakehub_use(lto_optimization)

# Compiler Options Management
#cmakehub_use(compile_options)

# ==============================================================================
# Code Quality Modules
# ==============================================================================

# Compiler Warnings
#cmakehub_use(compiler_warnings)

# Static Analysis Tools (clang-tidy, IWYU, cppcheck)
#cmakehub_use(clang_tidy_tools)

# Code Formatting (clang-format, cmake-format)
#cmakehub_use(code_formatter)

# ==============================================================================
# Testing Modules
# ==============================================================================

# Google Test Integration Note: Temporarily disabled due to master branch issue
# (googletest uses main branch now) Will manually configure GTest later using
# CPM cmakehub_use(add_gtest)

# Code Coverage (gcov/lcov) Note: Temporarily disabled on Windows (gcov not
# available) Will enable on Linux/macOS cmakehub_use(coverage)

# ==============================================================================
# Dependency Management
# ==============================================================================

# Enable shallow clone by default (faster)
set(CPM_DOWNLOAD_GIT_SHALLOW
    TRUE
    CACHE BOOL "Use shallow git clones for faster downloads")

# Set CPM source cache for faster rebuilds
if(NOT DEFINED CPM_SOURCE_CACHE)
  set(CPM_SOURCE_CACHE
      "${CMAKE_CURRENT_SOURCE_DIR}/.cpm-cache"
      CACHE PATH "CPM source cache directory")
endif()

message(STATUS "CPM source cache: ${CPM_SOURCE_CACHE}")

# Lightweight CMake Package Manager
cmakehub_use(cpm)

# ==============================================================================
# CPM Dependencies
# ==============================================================================

# Note: After cmakehub use(cpm) is processed, we can add dependencies All
# dependencies will be downloaded and configured automatically

# ==============================================================================
# CPM Proxy Configuration
# ==============================================================================

# Set up proxy for CPM to speed up downloads in China CPM respects HTTP_PROXY
# and HTTPS_PROXY environment variables You can set them before running cmake:
# set HTTP_PROXY=http://127.0.0.1:7890 set HTTPS_PROXY=http://127.0.0.1:7890
# cmake .. -G "Ninja"

# Or configure proxy via CMake variables
if(DEFINED ENV{HTTP_PROXY})
  message(STATUS "Using HTTP proxy: $ENV{HTTP_PROXY}")
  set(CPM_DOWNLOAD_HTTP_PROXY "$ENV{HTTP_PROXY}")
endif()

if(DEFINED ENV{HTTPS_PROXY})
  message(STATUS "Using HTTPS proxy: $ENV{HTTPS_PROXY}")
  set(CPM_DOWNLOAD_HTTPS_PROXY "$ENV{HTTPS_PROXY}")
endif()

# ==============================================================================
# Git Proxy Configuration
# ==============================================================================

# CPM uses Git to clone repositories, so we need to configure Git proxy too For
# SOCKS5 proxy (like your 7897 port), Git supports it natively

function(configure_git_proxy proxy_env)
  if(NOT proxy_env)
    return()
  endif()

  # Extract protocol, host and port from proxy URL Supports formats:
  # http://127.0.0.1:7897, socks5://127.0.0.1:7897, socks5h://127.0.0.1:7897
  if(proxy_env MATCHES "^([a-z]+)://([^:]+):([0-9]+)$")
    set(PROXY_PROTOCOL ${CMAKE_MATCH_1})
    set(PROXY_HOST ${CMAKE_MATCH_2})
    set(PROXY_PORT ${CMAKE_MATCH_3})

    # Determine Git proxy protocol Git supports: http, https, socks5, socks5h
    string(TOLOWER ${PROXY_PROTOCOL} PROXY_PROTOCOL_LOWER)

    if(PROXY_PROTOCOL_LOWER STREQUAL "socks5" OR PROXY_PROTOCOL_LOWER STREQUAL
                                                 "socks5h")
      set(GIT_PROXY "${PROXY_PROTOCOL_LOWER}://${PROXY_HOST}:${PROXY_PORT}")
    else()
      # For HTTP proxies, Git also works with http:// protocol
      set(GIT_PROXY "http://${PROXY_HOST}:${PROXY_PORT}")
    endif()

    # Configure Git to use the proxy
    execute_process(COMMAND git config --global http.proxy "${GIT_PROXY}"
                    OUTPUT_QUIET ERROR_QUIET)

    execute_process(COMMAND git config --global https.proxy "${GIT_PROXY}"
                    OUTPUT_QUIET ERROR_QUIET)

    message(STATUS "Configured Git proxy: ${GIT_PROXY}")
  else()
    message(WARNING "Invalid proxy format: ${proxy_env}")
  endif()
endfunction()

# Configure Git proxy based on HTTP_PROXY or HTTPS_PROXY
if(DEFINED ENV{HTTP_PROXY})
  configure_git_proxy("$ENV{HTTP_PROXY}")
elseif(DEFINED ENV{HTTPS_PROXY})
  configure_git_proxy("$ENV{HTTPS_PROXY}")
endif()

# Alternative: Set proxy directly in CMakeLists.txt Uncomment and configure if
# needed: configure_git_proxy("socks5://127.0.0.1:7897")

# ==============================================================================
# GitHub Mirror Configuration (Speed Up Downloads)
# ==============================================================================

# Configure Git to use GitHub mirror for faster downloads in China Uncomment one
# of the mirrors below to use it:

# Option 1: ghproxy (fastest) Note: Disabled due to URL parsing issues. Using
# proxy only. execute_process( COMMAND git config --global
# url."https://ghproxy.com/github.com/".insteadOf "https://github.com/"
# OUTPUT_QUIET ERROR_QUIET ) message(STATUS "Using GitHub mirror: ghproxy.com")
message(STATUS "Using proxy only (GitHub mirror disabled)")

# Option 2: mirror.ghproxy (alternative) execute_process( COMMAND git config
# --global url."https://mirror.ghproxy.com/https://github.com/".insteadOf
# "https://github.com/" OUTPUT_QUIET ERROR_QUIET ) message(STATUS "Using GitHub
# mirror: mirror.ghproxy.com")

# Option 3: Gitee (GitHub mirror on Gitee) Note: You need to manually sync the
# repository to Gitee first execute_process( COMMAND git config --global
# url."https://gitee.com/mirrors/".insteadOf "https://github.com/" OUTPUT_QUIET
# ERROR_QUIET ) message(STATUS "Using GitHub mirror: gitee.com")

# Option 4: gitclone (another fast mirror) execute_process( COMMAND git config
# --global url."https://gitclone.com/github.com/".insteadOf
# "https://github.com/" OUTPUT_QUIET ERROR_QUIET ) message(STATUS "Using GitHub
# mirror: gitclone.com")

# ==============================================================================
# Pre-dependency Configuration
# ==============================================================================

# Disable -Werror for third-party libraries BEFORE adding packages
# These settings must be set before CPMAddPackage calls
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(GTEST_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(FMT_COMPILE FALSE CACHE BOOL "" FORCE)

# ==============================================================================
# Core Dependencies

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

CPMAddPackage(
  NAME
  gflags
  VERSION
  2.2.2
  URL
  https://github.com/gflags/gflags/archive/refs/tags/v2.2.2.tar.gz
  URL_HASH
  SHA256=34af2f15cf7367513b352bdcd2493ab14ce43692d2dcd9dfc499492966c64dcf
  OPTIONS
  "BUILD_SHARED_LIBS OFF"
  "BUILD_STATIC_LIBS ON"
  "CMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/gflags")

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/gflags")


message(STATUS "🔧 Configuring libgossip dependencies...")
CPMAddPackage(
  NAME
  nlohmann_json
  VERSION
  3.11.2
  URL
  https://github.com/nlohmann/json/archive/refs/tags/v3.11.2.tar.gz
  URL_HASH
  SHA256=d69f9deb6a75e2580465c6c4c5111b89c4dc2fa94e3a85fcd2ffcd9a143d9273
  DOWNLOAD_ONLY
  YES)

set(LIBGOSSIP_JSON_DIR
    "${libgossip_SOURCE_DIR}/third_party/json/single_include")
file(MAKE_DIRECTORY "${LIBGOSSIP_JSON_DIR}")

set(JSON_SOURCE_DIR "${nlohmann_json_SOURCE_DIR}/single_include/nlohmann")
set(JSON_TARGET_DIR "${LIBGOSSIP_JSON_DIR}/nlohmann")

if(NOT EXISTS "${JSON_TARGET_DIR}")
  message(STATUS "Injecting json into ${libgossip_SOURCE_DIR}")
  file(COPY "${JSON_SOURCE_DIR}" DESTINATION "${LIBGOSSIP_JSON_DIR}/")
  message(STATUS "Done")
endif()

message(STATUS "libgossip dir: ${libgossip_SOURCE_DIR}")
message(STATUS "json location: ${JSON_TARGET_DIR}")

# Asio - C++ Network Library with Coroutines
CPMAddPackage(
  NAME
  asio
  VERSION
  1.30.2
  URL
  https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.tar.gz
  OPTIONS
  "ASIO_STANDALONE ON")

# fmt - Formatting library (for spdlog)
# Use fmt 12.1.0 for latest C++20 support
set(_SAVED_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "-Werror" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
CPMAddPackage(
  NAME
  fmt
  VERSION
  12.1.0
  URL
  https://github.com/fmtlib/fmt/archive/refs/tags/12.1.0.tar.gz
  OPTIONS
  "FMT_TEST OFF"
  "FMT_DOC OFF"
  "FMT_INSTALL OFF")
set(CMAKE_CXX_FLAGS "${_SAVED_CMAKE_CXX_FLAGS}")

# Disable -Werror for fmt
if(fmt_ADDED)
  if(TARGET fmt)
    target_compile_options(fmt PRIVATE -Wno-error)
  endif()
endif()

# spdlog - Fast C++ Logging Library
# Use spdlog 1.17.0 with external fmt for better C++20 compatibility
CPMAddPackage(
  NAME
  spdlog
  VERSION
  1.17.0
  URL
  https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz
  OPTIONS
  "SPDLOG_BUILD_SHARED OFF"
  "SPDLOG_FMT_EXTERNAL ON"
  "SPDLOG_FMT_HEADER_ONLY OFF"
  "SPDLOG_BUILD_TESTS OFF"
  "SPDLOG_BUILD_EXAMPLE OFF")

# Google Test - Unit Testing Framework
# Add before RocksDB to avoid gtest target conflict
CPMAddPackage(
  NAME
  googletest
  VERSION
  1.14.0
  URL
  https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  OPTIONS
  "BUILD_GMOCK ON"
  "INSTALL_GTEST OFF")

# LevelDB - Lightweight Key-Value Store

CPMAddPackage(

  NAME

  leveldb

  VERSION

  1.23

  GITHUB_REPOSITORY

  google/leveldb

  GIT_TAG

  1.23

  OPTIONS

  "LEVELDB_BUILD_TESTS OFF"

  "LEVELDB_BUILD_BENCHMARKS OFF"

  "LEVELDB_INSTALL OFF")



# Disable -Werror for LevelDB to avoid deprecated warnings

if(leveldb_ADDED)

  if(TARGET leveldb)

    target_compile_options(leveldb PRIVATE -Wno-error)

    # Force disable -Werror in leveldb's compile flags

    get_target_property(LEVELDB_COMPILE_OPTS leveldb COMPILE_OPTIONS)

    if(LEVELDB_COMPILE_OPTS)

      list(REMOVE_ITEM LEVELDB_COMPILE_OPTS "-Werror")

      set_target_properties(leveldb PROPERTIES COMPILE_OPTIONS "${LEVELDB_COMPILE_OPTS}")

    endif()

  endif()

endif()

# libgossip - Gossip Protocol for Cluster Communication
CPMAddPackage(
  NAME
  libgossip
  VERSION
  1.2.0
  URL
  https://github.com/caomengxuan666/libgossip/archive/master.tar.gz
  OPTIONS
  "BUILD_PYTHON_BINDINGS OFF"
  "BUILD_EXAMPLES OFF"
  "BUILD_TESTS OFF")

# FlatBuffers - Zero-Copy Serialization
CPMAddPackage(
  NAME
  flatbuffers
  VERSION
  24.3.25
  GITHUB_REPOSITORY
  google/flatbuffers
  GIT_TAG
  v24.3.25) # Google Benchmark - Performance Benchmarking
# Set environment variable to disable -Werror before adding the package
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "Disable -Werror for benchmark" FORCE)
set(BENCHMARK_FORCE_WERROR OFF CACHE BOOL "Force disable -Werror" FORCE)
CPMAddPackage(
  NAME benchmark
  VERSION 1.8.5
  URL https://github.com/google/benchmark/archive/refs/tags/v1.8.5.tar.gz
  OPTIONS "BENCHMARK_ENABLE_TESTING OFF" "BENCHMARK_ENABLE_GTEST_TESTS OFF" "BENCHMARK_ENABLE_INSTALL OFF" "BENCHMARK_ENABLE_WERROR OFF" "BENCHMARK_FORCE_WERROR OFF")

# Disable specific warnings for benchmark
if(benchmark_ADDED)
  if(TARGET benchmark)
    target_compile_options(benchmark PRIVATE -Wno-invalid-offsetof -Wno-switch)
  endif()
  if(TARGET benchmark_main)
    target_compile_options(benchmark_main PRIVATE -Wno-invalid-offsetof -Wno-switch)
  endif()
endif() # Optional High-Performance Libraries

# Folly - Facebook Open-source Library Note: Folly is complex and may take time
# to build Uncomment if needed: CPMAddPackage( NAME folly VERSION 2024.02.05.00
# GITHUB_REPOSITORY facebook/folly GIT_TAG v2024.02.05.00 )# Boost -
# Peer-reviewed Portable C++ Source Libraries Note: Only include specific Boost
# libraries needed Uncomment if needed: CPMAddPackage( NAME boost VERSION 1.85.0
# URL https://archives.boost.io/release/1.85.0/source/boost_1_85_0.tar.gz )

# Abseil - Common C++ Libraries Note: Only include if needed for specific
# functionality Uncomment if needed: CPMAddPackage( NAME abseil VERSION
# 20240116.1 GITHUB_REPOSITORY abseil/abseil-cpp GIT_TAG 20240116.1 )
#
# ==============================================================================
# Dependency Fixes
# ==============================================================================

# Inject ASIO into libgossip's third_party directory to fix asio.hpp not found
if(libgossip_ADDED AND asio_ADDED)
  message(STATUS "🔧 Fixing libgossip ASIO dependency...")

  set(LIBGOSSIP_ASIO_TARGET "${libgossip_SOURCE_DIR}/third_party/asio/asio/include")
  file(MAKE_DIRECTORY "${LIBGOSSIP_ASIO_TARGET}")

  file(COPY "${asio_SOURCE_DIR}/asio/include/asio"
      DESTINATION "${LIBGOSSIP_ASIO_TARGET}/")
  file(COPY "${asio_SOURCE_DIR}/asio/include/asio.hpp"
      DESTINATION "${LIBGOSSIP_ASIO_TARGET}/")

  if(EXISTS "${LIBGOSSIP_ASIO_TARGET}/asio.hpp")
    message(STATUS "✅ ASIO successfully injected into libgossip")
  else()
    message(WARNING "⚠️ ASIO injection failed")
  endif()
endif()

# Print current status for debugging
message(STATUS "📌 asio dir: ${asio_SOURCE_DIR}")
message(STATUS "📌 libgossip dir: ${libgossip_SOURCE_DIR}")

# ==============================================================================
# Documentation and Tools
# ==============================================================================
# Doxygen Documentation Generation
cmakehub_use(doxygen_helper)

# Git Version Management
cmakehub_use(git_version)

# ==============================================================================
# Debugging Tools
# ==============================================================================

# ==============================================================================
# C++20 Configuration
# ==============================================================================

# Set C++20 standard

set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard")

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS OFF)

# Enable all warnings
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  # Note: -Werror disabled due to C++20 compatibility issues with third-party libraries (fmt, spdlog)
endif()

# ==============================================================================
# CPM - Dependency Management
# ==============================================================================

# After CPM is included, we can add dependencies Note: These will be added after
# cmakehub use(cpm) is processed

# ==============================================================================
# Project Options
# ==============================================================================

option(ASTRADB_BUILD_TESTS "Build unit tests" ON)
option(ASTRADB_BUILD_BENCHMARKS "Build performance benchmarks" ON)
option(ASTRADB_BUILD_DOCS "Build documentation" ON)
option(ASTRADB_ENABLE_COVERAGE "Enable code coverage" OFF)
option(ASTRADB_ENABLE_SANITIZERS "Enable sanitizers (debug mode)" OFF)
option(ASTRADB_ENABLE_LTO "Enable Link Time Optimization (release mode)" ON)

# ==============================================================================
# Build Type Configuration
# ==============================================================================

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE
      Release
      CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ==============================================================================
# Platform-Specific Settings
# ==============================================================================

if(WIN32)
  # Windows-specific settings
  add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
  message(STATUS "Platform: Windows")
elseif(APPLE)
  # macOS-specific settings
  message(STATUS "Platform: macOS")
else()
  # Linux-specific settings
  message(STATUS "Platform: Linux")
endif()

# ==============================================================================
# Source Files
# ==============================================================================

# Collect source files - handle both initial and full project structure
file(
  GLOB_RECURSE
  ASTRADB_SOURCES
  "src/*.cpp"
  "src/core/*.cpp"
  "src/network/*.cpp"
  "src/storage/*.cpp"
  "src/cluster/*.cpp"
  "src/protocol/*.cpp"
  "src/server/*.cpp"
  "src/utils/*.cpp")

file(
  GLOB_RECURSE
  ASTRADB_HEADERS
  "src/*.h"
  "src/*.hpp"
  "src/core/*.h"
  "src/network/*.h"
  "src/storage/*.h"
  "src/cluster/*.h"
  "src/protocol/*.h"
  "src/server/*.h"
  "src/utils/*.h"
  "src/core/*.hpp"
  "src/network/*.hpp"
  "src/storage/*.hpp"
  "src/cluster/*.hpp"
  "src/protocol/*.hpp"
  "src/server/*.hpp"
  "src/utils/*.hpp")

# ==============================================================================
# Main Executable
# ==============================================================================

add_executable(astradb ${ASTRADB_SOURCES} ${ASTRADB_HEADERS})

# Also disable -Werror in global flags
string(REPLACE "-Werror" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Disable -Werror for astradb (put at end to override other options)
target_compile_options(astradb PRIVATE -Wno-error)

# ==============================================================================
# Target Configuration
# ==============================================================================

# Include directories
target_include_directories(astradb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
                                          ${CMAKE_CURRENT_BINARY_DIR})

# Add include directories for dependencies that don't provide targets
if(asio_ADDED)
  target_include_directories(astradb PRIVATE ${asio_SOURCE_DIR}/asio/include)
endif()

if(libgossip_ADDED)
  target_include_directories(astradb PRIVATE ${libgossip_SOURCE_DIR}/include)
endif()

if(flatbuffers_ADDED)
  target_include_directories(astradb PRIVATE ${flatbuffers_SOURCE_DIR}/include)
endif()

# Use target-based linking for dependencies that provide targets
if(TARGET leveldb)
  target_link_libraries(astradb PUBLIC leveldb)
endif()

if(TARGET fmt::fmt)
  target_link_libraries(astradb PUBLIC fmt::fmt)
endif()

if(TARGET spdlog::spdlog)
  target_link_libraries(astradb PUBLIC spdlog::spdlog)
endif()

if(TARGET flatbuffers::flatbuffers)
  target_link_libraries(astradb PUBLIC flatbuffers::flatbuffers)
endif()

if(TARGET GTest::gtest)
  target_link_libraries(astradb PUBLIC GTest::gtest)
endif()

if(TARGET benchmark::benchmark)
  target_link_libraries(astradb PUBLIC benchmark::benchmark)
endif()

# Link libraries - only link if the dependency is available
if(TARGET asio::asio)
  target_link_libraries(astradb PUBLIC asio::asio)
  target_compile_options(asio::asio PRIVATE -Wno-error)
  target_compile_definitions(asio::asio PRIVATE ASIO_DISABLE_SEH)
endif()

if(TARGET spdlog::spdlog)
  target_link_libraries(astradb PUBLIC spdlog::spdlog)
endif()

if(TARGET leveldb)
  target_link_libraries(astradb PUBLIC leveldb)
endif()

if(TARGET libgossip::libgossip)
  target_link_libraries(astradb PUBLIC libgossip::libgossip)
endif()

if(TARGET flatbuffers::flatbuffers)
  target_link_libraries(astradb PUBLIC flatbuffers::flatbuffers)
endif()

# Precompiled Headers (using cotire)
if(TARGET cotire)
  set_target_properties(astradb PROPERTIES COTIRE_ENABLE_PRECOMPILED_HEADER TRUE
                                           COTIRE_ADD_UNITY_BUILD TRUE)
endif()

# ==============================================================================
# Testing
# ==============================================================================

# Note: Temporarily disabled until test infrastructure is set up
# if(ASTRADB_BUILD_TESTS) enable_testing()
#
# # Test executable add_executable(astradb_tests tests/unit/*.cpp )
#
# target_include_directories(astradb_tests PUBLIC
# ${CMAKE_CURRENT_SOURCE_DIR}/src )
#
# # Link with GTest if(TARGET GTest::gtest) target_link_libraries(astradb_tests
# PRIVATE GTest::gtest) endif()
#
# # Register tests add_test(NAME astradb_unit_tests COMMAND astradb_tests)
# endif()

# ==============================================================================
# Documentation
# ==============================================================================

# Note: Temporarily disabled until docs directory is created
# if(ASTRADB_BUILD_DOCS) add_subdirectory(docs) endif()

# ==============================================================================
# Installation
# ==============================================================================

install(
  TARGETS astradb
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)

install(
  DIRECTORY src/
  DESTINATION include/astradb
  FILES_MATCHING
  PATTERN "*.h"
  PATTERN "*.hpp")

# ==============================================================================
# Configuration Summary
# ==============================================================================

message(STATUS "")
message(
  STATUS
    "==============================================================================="
)
message(STATUS "AstraDB Configuration Summary")
message(
  STATUS
    "==============================================================================="
)
message(STATUS "Version:             ${PROJECT_VERSION}")
message(STATUS "Build Type:          ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:        ${CMAKE_CXX_STANDARD}")
message(
  STATUS
    "Compiler:            ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)
message(STATUS "Build Tests:         ${ASTRADB_BUILD_TESTS}")
message(STATUS "Build Benchmarks:    ${ASTRADB_BUILD_BENCHMARKS}")
message(STATUS "Build Docs:          ${ASTRADB_BUILD_DOCS}")
message(STATUS "Enable Coverage:     ${ASTRADB_ENABLE_COVERAGE}")
message(STATUS "Enable Sanitizers:   ${ASTRADB_ENABLE_SANITIZERS}")
message(STATUS "Enable LTO:          ${ASTRADB_ENABLE_LTO}")
message(
  STATUS
    "==============================================================================="
)
message(STATUS "")
