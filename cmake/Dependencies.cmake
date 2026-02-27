# ==============================================================================
# Third-Party Dependencies
# ==============================================================================
# This module manages all third-party dependencies using CPM
# ==============================================================================

# ==============================================================================
# Core Dependencies
# ==============================================================================

# gflags - Command-line flags library
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

# ==============================================================================
# libgossip Dependencies
# ==============================================================================

message(STATUS "🔧 Configuring libgossip dependencies...")

# nlohmann_json - JSON library for libgossip
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

# ==============================================================================
# Network and Async Libraries
# ==============================================================================

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

# ==============================================================================
# Logging and Formatting
# ==============================================================================

# fmt - Formatting library (for spdlog)
# Use fmt 12.1.0 for latest C++20 support
set(_SAVED_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
if(NOT MSVC)
  string(REPLACE "-Werror" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()
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
    if(MSVC)
      # MSVC doesn't use /WX to treat warnings as errors
    else()
      target_compile_options(fmt PRIVATE -Wno-error)
    endif()
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

# ==============================================================================
# Storage
# ==============================================================================

# Google Test - Unit Testing Framework
# Add before LevelDB to avoid any potential conflicts
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
    if(MSVC)
      # MSVC: No specific warnings to disable for now
    else()
      target_compile_options(leveldb PRIVATE -Wno-error)
      # Force disable -Werror in leveldb's compile flags
      get_target_property(LEVELDB_COMPILE_OPTS leveldb COMPILE_OPTIONS)
      if(LEVELDB_COMPILE_OPTS)
        list(REMOVE_ITEM LEVELDB_COMPILE_OPTS "-Werror")
        set_target_properties(leveldb PROPERTIES COMPILE_OPTIONS "${LEVELDB_COMPILE_OPTS}")
      endif()
    endif()
  endif()
endif()

# ==============================================================================
# Serialization and Networking
# ==============================================================================

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
  v24.3.25)

# ==============================================================================
# Testing and Benchmarking
# ==============================================================================

# Google Benchmark - Performance Benchmarking
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
    if(MSVC)
      # MSVC: /wd4577 disables 'noexcept used with no exception handling' warning
      # /wd4579 hides certain inlining issues
      target_compile_options(benchmark PRIVATE /wd4577 /wd4579)
    else()
      target_compile_options(benchmark PRIVATE -Wno-invalid-offsetof -Wno-switch)
    endif()
  endif()
  if(TARGET benchmark_main)
    if(MSVC)
      target_compile_options(benchmark_main PRIVATE /wd4577 /wd4579)
    else()
      target_compile_options(benchmark_main PRIVATE -Wno-invalid-offsetof -Wno-switch)
    endif()
  endif()
endif()

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
# Optional High-Performance Libraries
# ==============================================================================

# Folly - Facebook Open-source Library
# Note: Folly is complex and may take time to build
# Uncomment if needed:
# CPMAddPackage(
#   NAME folly
#   VERSION 2024.02.05.00
#   GITHUB_REPOSITORY facebook/folly
#   GIT_TAG v2024.02.05.00
# )

# Boost - Peer-reviewed Portable C++ Source Libraries
# Note: Only include specific Boost libraries needed
# Uncomment if needed:
# CPMAddPackage(
#   NAME boost
#   VERSION 1.85.0
#   URL https://archives.boost.io/release/1.85.0/source/boost_1_85_0.tar.gz
# )

# Abseil - Common C++ Libraries
# Note: Only include if needed for specific functionality
# Uncomment if needed:
# CPMAddPackage(
#   NAME abseil
#   VERSION 20240116.1
#   GITHUB_REPOSITORY abseil/abseil-cpp
#   GIT_TAG 20240116.1
# )