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
  LANGUAGES CXX)

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

add_executable(astradb
  src/main.cpp
  src/astra/base/logging.cpp
)

target_include_directories(astradb PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_BINARY_DIR}
)

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

if(ASTRADB_STATIC_BUILD)
  target_link_options(astradb PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ==============================================================================
# Testing
# ==============================================================================

if(ASTRADB_BUILD_TESTS)
  enable_testing()
  
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/CMakeLists.txt)
    add_subdirectory(tests/unit)
  endif()
  
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/CMakeLists.txt)
    add_subdirectory(tests/integration)
  endif()
  
  if(ASTRADB_BUILD_BENCHMARKS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmark/CMakeLists.txt)
    add_subdirectory(tests/benchmark)
  endif()
endif()

# ==============================================================================
# Configuration Summary
# ==============================================================================

include(cmake/ConfigSummary.cmake)