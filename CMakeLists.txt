# ==============================================================================
# AstraDB - High-Performance Redis-Compatible Database
# ==============================================================================
# License: Apache 2.0
# Target: 2x DragonflyDB performance, 50% less memory usage
# Architecture: Shared-nothing multi-thread with C++23 coroutines
# ==============================================================================

cmake_minimum_required(VERSION 3.20)
project(
  AstraDB
  VERSION 1.0.0
  DESCRIPTION "Ultra-high performance Redis-compatible database"
  LANGUAGES CXX C)

# ==============================================================================
# Project Configuration
# ==============================================================================

option(ASTRADB_ENABLE_TLS "Enable TLS encryption support" ON)
option(ASTRADB_ENABLE_ACL "Enable ACL (Access Control List)" ON)
option(ASTRADB_ENABLE_SIMD "Enable SIMD optimizations" ON)

# ==============================================================================
# Load Configuration Modules
# ==============================================================================

include(cmake/EnvLoader.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/CPMConfig.cmake)
include(cmake/PlatformSettings.cmake)
include(cmake/ProjectOptions.cmake)

# ==============================================================================
# CMakeHub Integration
# ==============================================================================

set(CMH_CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.cmakehub/cache")
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/hub/loader.cmake)

cmakehub_use(precompiled_header)
cmakehub_use(lto_optimization)
cmakehub_use(compile_options)
cmakehub_use(compiler_warnings)
cmakehub_use(clang_tidy_tools)
cmakehub_use(code_formatter)
cmakehub_use(cpm)

# ==============================================================================
# Dependencies
# ==============================================================================

include(cmake/Dependencies.cmake)

# ==============================================================================
# OpenSSL for TLS support
# ==============================================================================

if(ASTRADB_ENABLE_TLS)
  find_package(OpenSSL REQUIRED)
  message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
  add_compile_definitions(ASTRADB_ENABLE_TLS)
endif()

# ==============================================================================
# Main Executable
# ==============================================================================

# Define version string for the application
add_definitions(-DASTRADB_VERSION_STRING="${PROJECT_VERSION}")

if(ASTRADB_BUILD_EXECUTABLE)
  add_executable(astradb
    src/main.cpp
    src/astra/base/logging.cpp
  )

  target_include_directories(astradb PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
  )

  target_link_libraries(astradb PRIVATE
    astra_server
    astra_commands
    astra_network
    astra_base
    astra_protocol
    spdlog::spdlog
    absl::base
  )

  # asio is header-only, just include directories
  if(asio_SOURCE_DIR)
    target_include_directories(astradb PRIVATE ${asio_SOURCE_DIR}/asio/include)
  endif()

  target_compile_features(astradb PRIVATE cxx_std_23)
endif()

# Add subdirectories
add_subdirectory(src/astra)

# ==============================================================================
# Testing
# ==============================================================================

# Disable mimalloc warnings that are treated as errors
if(TARGET mimalloc-static)
  target_compile_options(mimalloc-static PRIVATE -Wno-error)
endif()

# Link all third-party libraries (simplified approach)
target_link_libraries(astradb PRIVATE
  mimalloc-static
  spdlog::spdlog
  leveldb
  flatbuffers
  fmt::fmt
)

# Add Asio include directory
if(asio_SOURCE_DIR)
  target_include_directories(astradb PRIVATE ${asio_SOURCE_DIR}/asio/include)
endif()

# Add fmt include directory
if(fmt_SOURCE_DIR)
  target_include_directories(astradb PRIVATE ${fmt_SOURCE_DIR}/include)
endif()

# Link optional libraries
if(TARGET asio)
  target_link_libraries(astradb PRIVATE asio)
endif()

if(TARGET lua)
  target_link_libraries(astradb PRIVATE lua)
endif()

if(TARGET benchmark::benchmark)
  target_link_libraries(astradb PRIVATE benchmark::benchmark)
endif()

if(TARGET benchmark::benchmark_main)
  target_link_libraries(astradb PRIVATE benchmark::benchmark_main)
endif()

# Link OpenSSL if enabled
if(ASTRADB_ENABLE_TLS AND OPENSSL_FOUND)
  target_link_libraries(astradb PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  target_compile_definitions(astradb PRIVATE ASTRADB_ENABLE_TLS)
endif()

# Enable ACL if enabled
if(ASTRADB_ENABLE_ACL)
  target_compile_definitions(astradb PRIVATE ASTRADB_ENABLE_ACL)
endif()

# Enable SIMD if enabled
if(ASTRADB_ENABLE_SIMD)
  target_compile_definitions(astradb PRIVATE ASTRADB_ENABLE_SIMD)
endif()

# ==============================================================================
# Performance Optimizations
# ==============================================================================

# Link mode-specific settings
if(ASTRADB_LINK_MODE STREQUAL "FULL_STATIC")
  # Fully static linking
  target_link_options(astradb PRIVATE -static -static-libgcc -static-libstdc++)
  message(STATUS "Linker flags: -static -static-libgcc -static-libstdc++")
elseif(ASTRADB_LINK_MODE STREQUAL "PARTIAL_STATIC")
  # Partial static: static libraries, dynamic glibc
  target_link_options(astradb PRIVATE -static-libgcc -static-libstdc++)
  message(STATUS "Linker flags: -static-libgcc -static-libstdc++ (glibc dynamic)")
else()
  # Dynamic linking
  message(STATUS "Linker flags: none (dynamic linking)")
endif()

# ==============================================================================
# Testing
# ==============================================================================

enable_testing()
add_subdirectory(tests)

# Build unit tests
add_subdirectory(tests/unit)

# Build benchmarks
if(ASTRADB_BUILD_BENCHMARKS)
  add_subdirectory(tests/benchmark)
endif()

# ==============================================================================
# Configuration Summary
# ==============================================================================

include(cmake/ConfigSummary.cmake)