# ==============================================================================
# Third-Party Dependencies
# ==============================================================================
# This module manages all third-party dependencies using CPM
# ==============================================================================

# ==============================================================================
# Core Dependencies
# ==============================================================================

# mimalloc - High-performance memory allocator
# Features: Thread-local caching, size-classes, decay mechanism
# Benefits: 20-30% faster than glibc malloc, lower fragmentation
CPMAddPackage(
  NAME
  mimalloc
  VERSION
  2.1.7
  GITHUB_REPOSITORY
  microsoft/mimalloc
  GIT_TAG
  v2.1.7
  OPTIONS
  "MI_INSTALL OFF"
  "MI_BUILD_SHARED OFF"
  "MI_BUILD_STATIC ON"
  "MI_BUILD_OBJECT OFF"
  "MI_BUILD_TESTS OFF")

# xxHash - Ultra-fast hash algorithm
# Usage: Dashtable hashing, key sharding, CRC calculations
# Benefits: 10-100x faster than std::hash, essential for performance
CPMAddPackage(
  NAME
  xxhash
  VERSION
  0.8.2
  GITHUB_REPOSITORY
  Cyan4973/xxHash
  GIT_TAG
  v0.8.2
  OPTIONS
  "XXHASH_BUILD_TESTS OFF"
  "XXHASH_BUILD_CLI OFF"
  "XXHASH_BUILD_INSTALL OFF")

# zstd - Fast compression algorithm
# Usage: AOF file compression, RDB snapshot compression
# Benefits: Similar compression ratio to gzip, 3-5x faster
CPMAddPackage(
  NAME
  zstd
  VERSION
  1.5.6
  GITHUB_REPOSITORY
  facebook/zstd
  GIT_TAG
  v1.5.6
  OPTIONS
  "ZSTD_BUILD_TESTS OFF"
  "ZSTD_BUILD_PROGRAMS OFF"
  "ZSTD_BUILD_SHARED OFF"
  "ZSTD_BUILD_STATIC ON"
  "ZSTD_BUILD_CONTRIB OFF"
  "ZSTD_BUILD_LEGACY_SUPPORT OFF")

# Abseil - Common C++ Libraries
# Usage: High-performance containers (flat_hash_map, btree), string utilities
# Benefits: Better performance than STL containers, widely used in production
# Note: Only build what we need
CPMAddPackage(
  NAME
  abseil
  VERSION
  20240116.1
  GITHUB_REPOSITORY
  abseil/abseil-cpp
  GIT_TAG
  20240116.1
  OPTIONS
  "ABSL_ENABLE_INSTALL OFF"
  "ABSL_PROPAGATE_CXX_STD OFF"
  "BUILD_TESTING OFF")

# cxxopts - Lightweight command line parser
# Usage: Parse command line arguments for AstraDB server
# Benefits: Header-only, type-safe, modern C++, better than gflags
CPMAddPackage(
  NAME
  cxxopts
  VERSION
  3.2.1
  GITHUB_REPOSITORY
  jarro2783/cxxopts
  GIT_TAG
  v3.2.1)

# tomlplusplus - TOML parser
# Usage: Parse AstraDB configuration file (astradb.toml)
# Benefits: More readable than JSON, simpler than YAML, header-only
CPMAddPackage(
  NAME
  tomlplusplus
  VERSION
  3.4.0
  GITHUB_REPOSITORY
  marzer/tomlplusplus
  GIT_TAG
  v3.4.0
  OPTIONS
  "TOMLPLUSPLUS_BUILD_TESTS OFF"
  "TOMLPLUSPLUS_BUILD_EXAMPLES OFF")



# date - Modern date/time library by Howard Hinnant
# Usage: Timestamp handling, time calculations for TTL
# Benefits: Better than std::chrono, easier to use, timezone support
CPMAddPackage(
  NAME
  date
  VERSION
  3.0.3
  URL
  https://github.com/HowardHinnant/date/archive/refs/tags/v3.0.3.tar.gz
  DOWNLOAD_ONLY
  YES)

# concurrentqueue - Lock-free MPMC queue
# Usage: High-performance task queue for thread pool, command batching
# Benefits: No locks, no memory allocation, faster than std::queue with mutex
CPMAddPackage(
  NAME
  concurrentqueue
  GITHUB_REPOSITORY
  cameron314/concurrentqueue
  GIT_TAG
  v1.0.4)

# Intel TBB - Threading Building Blocks
# Usage: Work-stealing task scheduler, parallel algorithms, concurrent containers
# Benefits: Intel-proven, NUMA-aware, cache-friendly, automatic load balancing
CPMAddPackage(
  NAME
  TBB
  GITHUB_REPOSITORY
  oneapi-src/oneTBB
  GIT_TAG
  v2021.12.0
  OPTIONS
  "TBB_TEST OFF"
  "TBB_EXAMPLES OFF")

# Prometheus Client - Metrics collection and monitoring
# Usage: Export QPS, latency, memory usage, connection count
# Benefits: Production monitoring, Grafana integration, observability
# Reference: DragonflyDB uses Prometheus for metrics
CPMAddPackage(
  NAME
  prometheus-cpp
  VERSION
  1.2.2
  GITHUB_REPOSITORY
  jupp0r/prometheus-cpp
  GIT_TAG
  v1.2.2
  OPTIONS
  "ENABLE_PULL OFF"
  "ENABLE_PUSH OFF"
  "ENABLE_COMPRESSION OFF"
  "BUILD_TESTING OFF")

# Lua - Scripting support (Redis-compatible EVAL, SCRIPT commands)
# Usage: Server-side scripting, stored procedures, Lua 5.4
# Benefits: Full Redis compatibility, high performance scripting
# Reference: DragonflyDB uses Lua for Redis compatibility
CPMAddPackage(
  NAME
  lua
  VERSION
  5.4.7
  GITHUB_REPOSITORY
  lua/lua
  GIT_TAG
  v5.4.7
  DOWNLOAD_ONLY
  YES)

# Build Lua library manually
if(lua_ADDED)
  add_library(lua_lib STATIC
    ${lua_SOURCE_DIR}/lapi.c
    ${lua_SOURCE_DIR}/lauxlib.c
    ${lua_SOURCE_DIR}/lbaselib.c
    ${lua_SOURCE_DIR}/lcode.c
    ${lua_SOURCE_DIR}/lcorolib.c
    ${lua_SOURCE_DIR}/lctype.c
    ${lua_SOURCE_DIR}/ldblib.c
    ${lua_SOURCE_DIR}/ldebug.c
    ${lua_SOURCE_DIR}/ldo.c
    ${lua_SOURCE_DIR}/ldump.c
    ${lua_SOURCE_DIR}/lfunc.c
    ${lua_SOURCE_DIR}/lgc.c
    ${lua_SOURCE_DIR}/linit.c
    ${lua_SOURCE_DIR}/liolib.c
    ${lua_SOURCE_DIR}/llex.c
    ${lua_SOURCE_DIR}/lmathlib.c
    ${lua_SOURCE_DIR}/lmem.c
    ${lua_SOURCE_DIR}/loadlib.c
    ${lua_SOURCE_DIR}/lobject.c
    ${lua_SOURCE_DIR}/lopcodes.c
    ${lua_SOURCE_DIR}/loslib.c
    ${lua_SOURCE_DIR}/lparser.c
    ${lua_SOURCE_DIR}/lstate.c
    ${lua_SOURCE_DIR}/lstring.c
    ${lua_SOURCE_DIR}/lstrlib.c
    ${lua_SOURCE_DIR}/ltable.c
    ${lua_SOURCE_DIR}/ltablib.c
    ${lua_SOURCE_DIR}/ltm.c
    ${lua_SOURCE_DIR}/lundump.c
    ${lua_SOURCE_DIR}/lvm.c
    ${lua_SOURCE_DIR}/lzio.c
    ${lua_SOURCE_DIR}/lutf8lib.c
  )
  
  target_include_directories(lua_lib PUBLIC ${lua_SOURCE_DIR})
  target_compile_definitions(lua_lib PUBLIC LUA_COMPAT_5_3)
  
  # Create alias for easier linking
  add_library(lua::lua ALIAS lua_lib)
endif()

# ==============================================================================
# libgossip Dependencies
# ==============================================================================

message(STATUS "🔧 Configuring libgossip dependencies...")

# nlohmann_json - JSON library for libgossip
# Note: This must be downloaded before libgossip
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
  "BUILD_TESTS OFF"
  "BUILD_DOCS OFF"
  "INSTALL OFF")

# Inject nlohmann_json into libgossip's own third_party directory
# libgossip expects JSON at ${libgossip_SOURCE_DIR}/third_party/json/single_include/nlohmann
if(libgossip_ADDED AND nlohmann_json_ADDED)
  set(LIBGOSSIP_JSON_DIR "${libgossip_SOURCE_DIR}/third_party/json/single_include/nlohmann")
  file(MAKE_DIRECTORY "${LIBGOSSIP_JSON_DIR}")

  # Copy nlohmann header files to libgossip's third_party directory
  file(COPY "${nlohmann_json_SOURCE_DIR}/single_include/nlohmann/"
       DESTINATION "${LIBGOSSIP_JSON_DIR}/")

  message(STATUS "✅ nlohmann_json injected to libgossip's third_party: ${LIBGOSSIP_JSON_DIR}")
endif()

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

# Asio - Asynchronous I/O (C++23 coroutines)
CPMAddPackage(
  NAME
  asio
  GITHUB_REPOSITORY
  chriskohlhoff/asio
  GIT_TAG
  asio-1-30-8
  DOWNLOAD_ONLY
  YES)

# fmt - Formatting library (spdlog dependency)
CPMAddPackage(
  NAME
  fmt
  VERSION
  10.2.1
  GITHUB_REPOSITORY
  fmtlib/fmt
  GIT_TAG
  10.2.1
  OPTIONS
  "FMT_INSTALL OFF"
  "FMT_TEST OFF"
  "FMT_DOC OFF")

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
  "BUILD_GMOCK OFF"
  "INSTALL_GTEST OFF"
  "gtest_force_shared_crt OFF")

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
# Note: We use CPM's SOURCE_DIR option to pre-populate the directory
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
  "BUILD_TESTS OFF"
  "BUILD_DOCS OFF"
  "INSTALL OFF")

# Inject nlohmann_json into libgossip's third_party directory immediately after download
if(libgossip_ADDED AND nlohmann_json_ADDED)
  set(LIBGOSSIP_JSON_DIR "${libgossip_SOURCE_DIR}/third_party/json")
  file(MAKE_DIRECTORY "${LIBGOSSIP_JSON_DIR}")

  # Copy the entire single_include directory
  file(COPY "${nlohmann_json_SOURCE_DIR}/single_include/"
       DESTINATION "${LIBGOSSIP_JSON_DIR}/")
endif()

# FlatBuffers - Zero-Copy Serialization
CPMAddPackage(
  NAME
  flatbuffers
  VERSION
  24.3.25
  GITHUB_REPOSITORY
  google/flatbuffers
  GIT_TAG
  v24.3.25
  OPTIONS
  "FLATBUFFERS_BUILD_TESTS OFF"
  "FLATBUFFERS_BUILD_FLATC OFF"
  "FLATBUFFERS_BUILD_FLATHASH OFF"
  "FLATBUFFERS_BUILD_GRPCCPP OFF"
  "FLATBUFFERS_INSTALL OFF")

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
  set(LIBGOSSIP_ASIO_TARGET "${libgossip_SOURCE_DIR}/third_party/asio/asio/include")
  file(MAKE_DIRECTORY "${LIBGOSSIP_ASIO_TARGET}")

  file(COPY "${asio_SOURCE_DIR}/asio/include/asio"
      DESTINATION "${LIBGOSSIP_ASIO_TARGET}/")
  file(COPY "${asio_SOURCE_DIR}/asio/include/asio.hpp"
      DESTINATION "${LIBGOSSIP_ASIO_TARGET}/")
endif()

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