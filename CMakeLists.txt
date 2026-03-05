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

option(ASTRADB_ENABLE_TLS "Enable TLS encryption support" OFF)
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

# Precompiled Headers for faster compilation
include(cmake/cotire/PrecompiledHeader.cmake)
# ==============================================================================
# CMakeHub Integration
# ==============================================================================

set(CMH_CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.cmakehub/cache")
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/hub/loader.cmake)
cmakehub_use(lto_optimization)
cmakehub_use(compile_options)
cmakehub_use(compiler_warnings)
cmakehub_use(clang_tidy_tools)
cmakehub_use(code_formatter)
cmakehub_use(cpm)

# ==============================================================================
# Git Version Information
# ==============================================================================

# Load GetGitRevisionDescription module directly
include(${CMAKE_CURRENT_SOURCE_DIR}/.cmakehub/cache/git_version/master/cmake/GetGitRevisionDescription.cmake)

# Find Git first (required for git_version module)
find_package(Git QUIET)

# Get git revision using git_version module
get_git_head_revision(GIT_REFSPEC GIT_COMMIT_HASH)

# Get additional git information
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_SHORT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  
  execute_process(
    COMMAND ${GIT_EXECUTABLE} log -1 --format=%cd --date=iso
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  
  execute_process(
    COMMAND ${GIT_EXECUTABLE} diff-index --quiet HEAD --
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE GIT_IS_DIRTY
    ERROR_QUIET
  )
  
  if(NOT GIT_IS_DIRTY EQUAL 0)
    set(GIT_IS_DIRTY "1")
  else()
    set(GIT_IS_DIRTY "0")
  endif()
  
  message(STATUS "Git version: ${GIT_BRANCH} (${GIT_COMMIT_SHORT})")
else()
  set(GIT_COMMIT_SHORT "unknown")
  set(GIT_BRANCH "unknown")
  set(GIT_COMMIT_DATE "unknown")
  set(GIT_IS_DIRTY "0")
  message(STATUS "Git not found, using placeholder version info")
endif()

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
  
  # Enable Precompiled Headers for the main executable
  if(ENABLE_PCH)
    set_target_properties(astradb PROPERTIES
      COTIRE_ADD_PREFIX_HEADER_ADDITION TRUE
      COTIRE_UNITY_SOURCE TRUE
      COTIRE_CXX_PREFIX_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h"
    )
  endif()
endif()

# ==============================================================================
# Add subdirectories (source code)
# ==============================================================================
add_subdirectory(src/astra)

# ==============================================================================
# Testing
# ==============================================================================

enable_testing()

# Build unit tests
add_subdirectory(tests/unit)

# Build benchmarks
add_subdirectory(tests/benchmark)

# Build integration tests
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/CMakeLists.txt")
  add_subdirectory(tests/integration)
endif()

# ==============================================================================
# Enable Precompiled Headers with cotire
# ==============================================================================
# IMPORTANT: cotire must be configured AFTER all targets are fully set up
# including all their sources, include directories, and dependencies
# ==============================================================================

if(ENABLE_PCH)
  message(STATUS "Configuring Precompiled Headers...")
  set(COTIRE_VERBOSE ON)  # Enable verbose output for debugging
  
  # Apply cotire to core library targets
  if(TARGET astra_base)
    cotire(astra_base)
    get_target_property(_unitySource astra_base COTIRE_CXX_UNITY_SOURCE)
    get_target_property(_prefixHeader astra_base COTIRE_CXX_PREFIX_HEADER)
    get_target_property(_precompiledHeader astra_base COTIRE_CXX_PRECOMPILED_HEADER)
    message(STATUS "  - astra_base: unity=${_unitySource}, prefix=${_prefixHeader}, pch=${_precompiledHeader}")
  endif()
  if(TARGET astra_network)
    cotire(astra_network)
    message(STATUS "  - astra_network")
  endif()
  if(TARGET astra_commands)
    cotire(astra_commands)
    message(STATUS "  - astra_commands")
  endif()
  if(TARGET astra_server)
    cotire(astra_server)
    message(STATUS "  - astra_server")
  endif()
  if(TARGET astradb)
    cotire(astradb)
    message(STATUS "  - astradb")
  endif()
  
  # Ensure PCH targets are built before their dependent targets
  if(TARGET astra_base_pch)
    add_dependencies(astra_base astra_base_pch)
  endif()
  if(TARGET astra_commands_pch)
    add_dependencies(astra_commands astra_commands_pch)
  endif()
  if(TARGET astra_server_pch)
    add_dependencies(astra_server astra_server_pch)
  endif()
  
  message(STATUS "Precompiled Headers configured with explicit dependencies")
endif()

# ==============================================================================
# Version Header Generation
# ==============================================================================

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/base/version.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/astra/base/version.hpp
  @ONLY
)

# Add generated header to astra_base target include directories
target_include_directories(astra_base PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)

message(STATUS "Version header generated: ${CMAKE_CURRENT_BINARY_DIR}/include/astra/base/version.hpp")

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
# Link Time Optimization (LTO/IPO)
# ==============================================================================

# Enable LTO for all major targets
if(ASTRADB_ENABLE_LTO)
    foreach(target IN ITEMS astra_server astra_network astra_base astra_commands astradb)
        if(TARGET ${target})
            target_link_time_optimization(${target})
            message(STATUS "  [OK] LTO enabled for ${target}")
        endif()
    endforeach()
    
    # Alternatively, use global enable (but the above method is more controllable)
    # link_time_optimization()
endif()

# ==============================================================================
# Strip Debug Symbols for Release Builds
# ==============================================================================

if(CMAKE_BUILD_TYPE STREQUAL "Release" AND TARGET astradb)
    if(CMAKE_STRIP)
        add_custom_command(TARGET astradb POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Stripping debug symbols from astradb..."
            COMMAND ${CMAKE_STRIP} $<TARGET_FILE:astradb>
            COMMENT "Stripping debug symbols for release build"
        )
    else()
        message(WARNING "CMAKE_STRIP not found - debug symbols will remain in release build")
    endif()
endif()

# ==============================================================================
# Configuration Summary
# ==============================================================================

include(cmake/ConfigSummary.cmake)
