# ==============================================================================
# Third-Party Dependencies
# ==============================================================================
# This module manages all third-party dependencies using CPM
# ==============================================================================

# ==============================================================================
# Core Dependencies
# ==============================================================================

# SHA1 - SHA1 hash function implementation
# Usage: Lua script caching (EVALSHA, SCRIPT commands)
# Benefits: Lightweight, header-only, no OpenSSL dependency
CPMAddPackage(
        NAME
        sha1
        VERSION
        1.0.0
        GITHUB_REPOSITORY
        vog/sha1
        GIT_TAG
        master
        OPTIONS
        "SHA1_BUILD_TESTS OFF"
        "SHA1_BUILD_EXAMPLES OFF")

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

# Build zstd library manually (CPM doesn't create targets automatically)
if(zstd_ADDED)
  add_library(zstd_static STATIC
  # Common files
  ${zstd_SOURCE_DIR}/lib/common/entropy_common.c
  ${zstd_SOURCE_DIR}/lib/common/error_private.c
  ${zstd_SOURCE_DIR}/lib/common/zstd_common.c
  ${zstd_SOURCE_DIR}/lib/common/fse_decompress.c
  ${zstd_SOURCE_DIR}/lib/common/pool.c
  ${zstd_SOURCE_DIR}/lib/common/threading.c
  ${zstd_SOURCE_DIR}/lib/common/xxhash.c
  # Compress files
  ${zstd_SOURCE_DIR}/lib/compress/zstd_compress.c
  ${zstd_SOURCE_DIR}/lib/compress/zstd_compress_literals.c
  ${zstd_SOURCE_DIR}/lib/compress/zstd_compress_sequences.c
  ${zstd_SOURCE_DIR}/lib/compress/zstd_compress_superblock.c
  ${zstd_SOURCE_DIR}/lib/compress/zstd_double_fast.c
  ${zstd_SOURCE_DIR}/lib/compress/zstd_fast.c
  ${zstd_SOURCE_DIR}/lib/compress/zstd_lazy.c
  ${zstd_SOURCE_DIR}/lib/compress/zstd_ldm.c
  ${zstd_SOURCE_DIR}/lib/compress/zstd_opt.c
  ${zstd_SOURCE_DIR}/lib/compress/zstdmt_compress.c
  ${zstd_SOURCE_DIR}/lib/compress/huf_compress.c
  ${zstd_SOURCE_DIR}/lib/compress/fse_compress.c
  ${zstd_SOURCE_DIR}/lib/compress/hist.c
  # Decompress files
  ${zstd_SOURCE_DIR}/lib/decompress/zstd_decompress.c
  ${zstd_SOURCE_DIR}/lib/decompress/zstd_decompress_block.c
  ${zstd_SOURCE_DIR}/lib/decompress/zstd_ddict.c
  ${zstd_SOURCE_DIR}/lib/decompress/huf_decompress.c
  # DictBuilder files
  ${zstd_SOURCE_DIR}/lib/dictBuilder/cover.c
  ${zstd_SOURCE_DIR}/lib/dictBuilder/divsufsort.c
  ${zstd_SOURCE_DIR}/lib/dictBuilder/fastcover.c
  ${zstd_SOURCE_DIR}/lib/dictBuilder/zdict.c
  )
  
  target_include_directories(zstd_static PUBLIC ${zstd_SOURCE_DIR}/lib)
  target_compile_definitions(zstd_static PRIVATE ZSTD_LIB_DEPRECATED=1)
  
  # Create zstd::zstd alias for easier linking
  if(NOT TARGET zstd::zstd)
   add_library(zstd::zstd ALIAS zstd_static)
endif()
endif()

# Abseil - Common C++ Libraries
# Usage: High-performance containers (flat_hash_map, btree), string utilities
# Benefits: Better performance than STL containers, widely used in production
# Note: Only build what we need

# Set basic build options
set(ABSL_BUILD_OPTIONS
    "ABSL_ENABLE_INSTALL OFF"
    "ABSL_PROPAGATE_CXX_STD OFF"
    "BUILD_TESTING OFF")

# Disable hardware-accelerated instructions on macOS ARM64 to prevent SSE errors
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  # macOS ARM64 (Apple Silicon) does not support SSE instructions
  # Disable all hardware-accelerated AES to avoid compilation errors
  list(APPEND ABSL_BUILD_OPTIONS
    "ABSL_RANDOM_HWAES_ARM64 OFF"
    "ABSL_RANDOM_HWAES_MSVC OFF"
    "ABSL_RANDOM_HWAES_EMSCRIPTEN OFF"
    "ABSL_RANDOM_HWAES_DETECT_AES_SUPPORTED OFF"
    "ABSL_USE_ABSL_HASH OFF")  # Disable hash-based optimizations that may use SSE
  message(STATUS "🍎 Detected macOS ARM64, disabling abseil hardware acceleration")
endif()

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
        ${ABSL_BUILD_OPTIONS})

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

# Fix for macOS ARM64 atomic operations issue
# AppleClang on ARM64 has built-in atomic support, prometheus-cpp's CheckAtomic fails
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  set(HAVE_CXX_ATOMICS_WITHOUT_LIB 1 CACHE BOOL "" FORCE)
  set(HAVE_CXX_ATOMICS64_WITHOUT_LIB 1 CACHE BOOL "" FORCE)
endif()

CPMAddPackage(
  NAME
  prometheus-cpp
  VERSION
  1.2.4
  GITHUB_REPOSITORY
  jupp0r/prometheus-cpp
  GIT_TAG
  v1.2.4
  OPTIONS
  "ENABLE_PULL OFF"
  "ENABLE_PUSH OFF"
  "ENABLE_COMPRESSION OFF"
  "BUILD_TESTING OFF"
  "DISABLE_PULL_DEFAULT_EXPORT OFF")

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

  # Note: Lua's os.tmpname() uses tmpnam() which triggers a linker warning.
  # This is a known limitation of Lua 5.4's standard library.
  # The warning "the use of 'tmpnam' is dangerous, better use 'mkstemp'"
  # is generated by glibc at link time and cannot be easily suppressed.
  # See: https://www.lua.org/manual/5.4/manual.html#pdf-os.tmpname

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


# ==============================================================================

# Asio- C++ Network Library with Coroutines
CPMAddPackage(
        NAME
        asio
        VERSION
        1.30.2
        URL
        https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.tar.gz
        OPTIONS
        "ASIO_STANDALONE ON")

# Create asio interface target for main project
if(asio_ADDED)
  add_library(asio::asio INTERFACE IMPORTED)
  target_include_directories(asio::asio INTERFACE
          ${asio_SOURCE_DIR}/asio/include)

  # Check if io_uring backend is enabled
  if(ASTRADB_ENABLE_IO_URING)
    if(UNIX AND NOT APPLE)
      # Linux: try to enable io_uring backend
      find_library(LIBURING_LIB NAMES uring)
      if(LIBURING_LIB)
        # Create an interface library for io_uring support
        add_library(asio::io_uring INTERFACE IMPORTED)
        target_compile_definitions(asio::io_uring INTERFACE ASIO_HAS_IO_URING)
        target_link_libraries(asio::io_uring INTERFACE ${LIBURING_LIB})
        
        # Link io_uring library to asio target
        target_link_libraries(asio::asio INTERFACE asio::io_uring)
        
        message(STATUS "✅ Created asio::asio target with io_uring backend")
      else()
        message(WARNING "liburing not found, falling back to epoll backend")
        message(STATUS "✅ Created asio::asio target with epoll backend (io_uring requested but not available)")
      endif()
    else()
      message(WARNING "io_uring is only supported on Linux, falling back to epoll backend")
      message(STATUS "✅ Created asio::asio target with epoll backend (io_uring requested but platform unsupported)")
    endif()
  else()
    # Default: use epoll backend
    message(STATUS "✅ Created asio::asio target with epoll backend (io_uring disabled)")
  endif()
endif()

# ==============================================================================

# fmt - Formatting library (spdlog dependency)
# libgossip - Gossip Protocol for Cluster Communication
# First, download the package without adding it
CPMAddPackage(
        NAME
        libgossip_download
        VERSION
        1.2.1.3
        URL
        https://github.com/caomengxuan666/libgossip/archive/refs/tags/v1.2.1.3.tar.gz
        DOWNLOAD_ONLY
        YES)
# Set LIBGOSSIP_SOURCE variable
set(LIBGOSSIP_SOURCE "${libgossip_download_SOURCE_DIR}")

# Inject asio immediately after setting source
if(asio_ADDED)
  set(LIBGOSSIP_ASIO_TARGET "${LIBGOSSIP_SOURCE}/third_party/asio/asio/include")
  file(MAKE_DIRECTORY "${LIBGOSSIP_ASIO_TARGET}")

  file(COPY "${asio_SOURCE_DIR}/asio/include/asio"
          DESTINATION "${LIBGOSSIP_ASIO_TARGET}/")
  file(COPY "${asio_SOURCE_DIR}/asio/include/asio.hpp"
          DESTINATION "${LIBGOSSIP_ASIO_TARGET}/")

  message(STATUS "✅ ASIO injected to libgossip's third_party: ${LIBGOSSIP_ASIO_TARGET}")

  # Inject nlohmann_json immediately after asio
  if(nlohmann_json_ADDED)
    set(LIBGOSSIP_JSON_TARGET "${LIBGOSSIP_SOURCE}/third_party/json/single_include/nlohmann")
    file(MAKE_DIRECTORY "${LIBGOSSIP_JSON_TARGET}")

    file(COPY "${nlohmann_json_SOURCE_DIR}/single_include/nlohmann/"
            DESTINATION "${LIBGOSSIP_JSON_TARGET}/")

    message(STATUS "✅ nlohmann_json injected to libgossip's third_party: ${LIBGOSSIP_JSON_TARGET}")
  endif()
  # Set options to skip third_party checks, tests, examples, and Python bindings
  set(LIBGOSSIP_SKIP_THIRD_PARTY_CHECK ON CACHE BOOL "Skip third_party checks for libgossip" FORCE)
  set(BUILD_TESTS OFF CACHE BOOL "Build tests for libgossip" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "Build examples for libgossip" FORCE)
  set(BUILD_PYTHON_BINDINGS OFF CACHE BOOL "Build Python bindings for libgossip" FORCE)

  # Now add the patched directory
  add_subdirectory("${LIBGOSSIP_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}/libgossip")

  # Set the _ADDED variable manually
  set(libgossip_ADDED ON)
  set(libgossip_SOURCE_DIR "${LIBGOSSIP_SOURCE}")
endif()

# ==============================================================================

# Network and Async Libraries
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
if(ASTRADB_BUILD_TESTS)
  CPMAddPackage(
          NAME
          googletest
          VERSION
          1.14.0
          URL
          https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
          OPTIONS
          "BUILD_GMOCK ON"  # Enable Google Mock for unit tests
          "INSTALL_GTEST OFF"
          "gtest_force_shared_crt OFF")
endif()

# RocksDB - High Performance Key-Value Store
# Usage: Alternative to LevelDB for persistence (better write performance)
# Benefits: Better write performance, compression, multithreading
CPMAddPackage(
        NAME
        rocksdb
        VERSION
        10.10.1
        URL
        https://github.com/facebook/rocksdb/archive/refs/tags/v10.10.1.tar.gz
        OPTIONS
        "WITH_TESTS OFF"
        "WITH_BENCHMARK_TOOLS OFF"
        "WITH_TOOLS OFF"
        "WITH_CORETOOLS OFF"
        "WITH_FATAL_ERROR_HANDLER OFF"
        "WITH_XPRESS OFF"
        "WITH_ZSTD OFF"
        "WITH_LZ4 OFF"        "WITH_ZLIB ON"
        "WITH_SNAPPY OFF"
        "WITH_GFLAGS OFF"
        "USE_RTTI ON"
        "ROCKSDB_BUILD_SHARED OFF"
        "ROCKSDB_INSTALL OFF"
        "FAIL_ON_WARNINGS OFF"
        "CMAKE_SKIP_INSTALL_RULES ON")

# Create zstd_static alias for RocksDB
if(TARGET zstd::zstd AND NOT TARGET zstd_static)
  add_library(zstd_static ALIAS zstd::zstd)
endif()

# Disable -Werror for RocksDB to avoid warnings
if(rocksdb_ADDED)
  if(TARGET rocksdb)
    if(MSVC)
      # MSVC: No specific warnings to disable for now
    else()
      target_compile_options(rocksdb PRIVATE -Wno-error)
    endif()
  endif()
  
  # Create an alias target if it doesn't exist
  if(TARGET rocksdb AND NOT TARGET rocksdb::rocksdb)
    add_library(rocksdb::rocksdb ALIAS rocksdb)
  endif()
endif()

# ==============================================================================

# Serialization and Networking

# ==============================================================================

# FlatBuffers - Zero-Copy Serialization

# Note: Enable FLATBUFFERS_BUILD_FLATC to generate code from .fbs files

# Disable unused language generators to speed up compilation

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

        "FLATBUFFERS_BUILD_FLATC ON"  # Enable flatc compiler for code generation

        "FLATBUFFERS_BUILD_FLATHASH OFF"

        "FLATBUFFERS_BUILD_GRPCCPP OFF"

        "FLATBUFFERS_BUILD_JAVA OFF"  # Disable Java

        "FLATBUFFERS_BUILD_CSHARP OFF"  # Disable C#

        "FLATBUFFERS_BUILD_GO OFF"  # Disable Go

        "FLATBUFFERS_BUILD_PYTHON OFF"  # Disable Python

        "FLATBUFFERS_BUILD_PHP OFF"  # Disable PHP

        "FLATBUFFERS_BUILD_NODEJS OFF"  # Disable Node.js

        "FLATBUFFERS_BUILD_TS OFF"  # Disable TypeScript

        "FLATBUFFERS_BUILD_LOBBY OFF"  # Disable Lobster

        "FLATBUFFERS_BUILD_LUA OFF"  # Disable Lua

        "FLATBUFFERS_BUILD_RUST OFF"  # Disable Rust

        "FLATBUFFERS_BUILD_SWIFT OFF"  # Disable Swift

        "FLATBUFFERS_BUILD_KOTLIN OFF"  # Disable Kotlin

        "FLATBUFFERS_BUILD_DART OFF"  # Disable Dart

        "FLATBUFFERS_BUILD_GRPC OFF"  # Disable gRPC

        "FLATBUFFERS_BUILD_INSTALL OFF")

# ==============================================================================
# Testing and Benchmarking
# ==============================================================================

# Google Benchmark - Performance Benchmarking
if(ASTRADB_BUILD_BENCHMARKS)
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
endif()


# ==============================================================================
# Post-Dependency Injection
# ==============================================================================
include(${CMAKE_CURRENT_LIST_DIR}/PostDependencies.cmake)
