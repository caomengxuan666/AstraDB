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
option(ASTRADB_ENABLE_IO_URING "Enable io_uring backend (Linux only, experimental)" OFF)
option(ASTRADB_STRIP_SYMBOLS "Strip debug symbols from release builds" ON)

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
cmakehub_use(lto_optimization)
cmakehub_use(compile_options OPTION_BUILD_TESTS OFF)
cmakehub_use(compiler_warnings)
cmakehub_use(clang_tidy_tools)
cmakehub_use(code_formatter)
cmakehub_use(cpm)

# ==============================================================================
# Git Version Information
# ==============================================================================
# CMakeHub - Unified CMake Module Manager
# ==============================================================================

# Load CMakeHub loader
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/hub/loader.cmake)

# Load GetGitRevisionDescription module from our own cmake/modules/
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/GetGitRevisionDescription.cmake)

# Read from .env
read_env_file()

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
# Generated Headers Management
# ==============================================================================
include(cmake/GeneratedHeaders.cmake)

# ==============================================================================
# FLATBUFFERS PREBUILD - MUST COMPILE FIRST!
# ==============================================================================
# This generates all FlatBuffers headers BEFORE any subproject compilation
# Nothing can compile until these headers are ready!
# ==============================================================================
include(cmake/prebuild.cmake)

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
  
  # PCH is configured below using CMake 3.16+ native support
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
# Enable Precompiled Headers with CMake 3.16+ Native Support
# ==============================================================================
if(ENABLE_PCH)
  message(STATUS "Configuring Precompiled Headers with CMake 3.16+ native support...")
  
  # Apply PCH to core library targets using CMake 3.16+ native support
  if(TARGET astra_base)
    target_precompile_headers(astra_base PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h)
    message(STATUS "  - astra_base: PCH enabled")
  endif()
  if(TARGET astra_network)
    target_precompile_headers(astra_network PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h)
    message(STATUS "  - astra_network: PCH enabled")
  endif()
  if(TARGET astra_commands)
    target_precompile_headers(astra_commands PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h)
    message(STATUS "  - astra_commands: PCH enabled")
  endif()
  if(TARGET astra_server)
    target_precompile_headers(astra_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h)
    message(STATUS "  - astra_server: PCH enabled")
  endif()
  if(TARGET astradb)
    target_precompile_headers(astradb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h)
    message(STATUS "  - astradb: PCH enabled")
  endif()
  
  message(STATUS "Precompiled Headers configured with CMake 3.16+ native support")
endif()

# ==============================================================================
# Link liburing for io_uring support on Linux (including WSL)
# ==============================================================================
if(UNIX AND NOT APPLE AND ASTRADB_ENABLE_IO_URING)
  find_library(LIBURING_LIB NAMES uring)
  if(LIBURING_LIB)
    message(STATUS "✅ Linux platform detected, found liburing: ${LIBURING_LIB}")
    # Link liburing to all targets that use ASIO on Linux
    if(TARGET astra_commands)
      target_link_libraries(astra_commands PUBLIC ${LIBURING_LIB})
    endif()
    if(TARGET astra_network)
      target_link_libraries(astra_network PUBLIC ${LIBURING_LIB})
    endif()
    if(TARGET astra_server)
      target_link_libraries(astra_server PUBLIC ${LIBURING_LIB})
    endif()
    if(TARGET astradb)
      target_link_libraries(astradb PRIVATE ${LIBURING_LIB})
    endif()
    message(STATUS "✅ Linked liburing for io_uring support on Linux")
  else()
    message(WARNING "⚠️  Linux platform but liburing not found, io_uring support will not be available")
  endif()
else()
  message(STATUS "ℹ️  Non-Linux platform detected, io_uring support not available (using epoll/kqueue instead)")
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

# Disable mimalloc warnings that are treated as errors (GCC/Clang only)
if(NOT MSVC AND TARGET mimalloc-static)
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

# Add zstd include directory
if(zstd_SOURCE_DIR)
  target_include_directories(astradb PRIVATE ${zstd_SOURCE_DIR}/lib)
endif()

# Add zstd include directory to astra_persistence (propagates to all dependent targets)
if(zstd_SOURCE_DIR AND TARGET astra_persistence)
  target_include_directories(astra_persistence INTERFACE ${zstd_SOURCE_DIR}/lib)
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

if(ASTRADB_STRIP_SYMBOLS AND CMAKE_BUILD_TYPE STREQUAL "Release" AND TARGET astradb)
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

# ==============================================================================
# CPack Configuration - Package Generation
# ==============================================================================
set(CPACK_PACKAGE_NAME "astradb")
set(CPACK_PACKAGE_VENDOR "caomengxuan666")
set(CPACK_PACKAGE_CONTACT "2507560089@qq.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AstraDB - Ultra-high performance Redis-compatible database")
set(CPACK_PACKAGE_DESCRIPTION "AstraDB is a high-performance, Redis-compatible database written in C++23. It aims to achieve 2x the performance of DragonflyDB with 50% less memory usage through shared-nothing multi-threaded architecture with C++23 coroutines.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/caomengxuan666/AstraDB")
set(CPACK_PACKAGE_LICENSE "Apache-2.0")
set(CPACK_PACKAGE_MAINTAINER "caomengxuan666 <2507560089@qq.com>")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})

# Install only the essential files - use PROGRAMS to avoid dependency tracking
install(PROGRAMS $<TARGET_FILE:astradb>
    DESTINATION bin
    COMPONENT Applications)

# Install configuration files
install(FILES astradb.toml
    DESTINATION etc/astradb
    COMPONENT Configuration)

# Install documentation
install(FILES README.md AstraDB_DESIGN.md PERFORMANCE.md LICENSE
    DESTINATION share/doc/astradb
    COMPONENT Documentation)

# Create component groups
set(CPACK_COMPONENTS_ALL Applications Configuration Documentation)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)

# Platform-specific configurations
if(UNIX AND NOT APPLE)
    # Linux-specific
    set(CPACK_GENERATOR "DEB;TGZ;RPM")
    
    # DEB package settings
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17), libgcc-s1, libstdc++6")
    set(CPACK_DEBIAN_PACKAGE_SECTION "database")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_COMPRESSION_TYPE "gzip")
    
    # Configure and install DEB control scripts
    configure_file(
        ${CMAKE_SOURCE_DIR}/packaging/deb/postinst.in
        ${CMAKE_CURRENT_BINARY_DIR}/deb/postinst
        @ONLY
    )
    configure_file(
        ${CMAKE_SOURCE_DIR}/packaging/deb/prerm.in
        ${CMAKE_CURRENT_BINARY_DIR}/deb/prerm
        @ONLY
    )
    
    # Make scripts executable
    install(PROGRAMS
        ${CMAKE_CURRENT_BINARY_DIR}/deb/postinst
        ${CMAKE_CURRENT_BINARY_DIR}/deb/prerm
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT Applications
    )
    
    # Install systemd service file
    install(FILES
        ${CMAKE_SOURCE_DIR}/packaging/systemd/astradb.service
        DESTINATION lib/systemd/system
        COMPONENT Applications
    )
    
    # Create postinst/prerm scripts for DEB
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_BINARY_DIR}/deb/postinst"
        "${CMAKE_CURRENT_BINARY_DIR}/deb/prerm")
    
elseif(WIN32)
    # Windows-specific
    set(CPACK_GENERATOR "NSIS;ZIP")
    
    # NSIS (MSI) package settings
    set(CPACK_NSIS_DISPLAY_NAME "AstraDB ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "AstraDB")
    set(CPACK_NSIS_CONTACT "2507560089@qq.com")
    set(CPACK_NSIS_HELP_LINK "https://github.com/caomengxuan666/AstraDB")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/caomengxuan666/AstraDB")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\astradb.exe")
    set(CPACK_NSIS_MODIFY_PATH ON)
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "astradb.exe")
    set(CPACK_NSIS_MENU_LINKS
        "bin/astradb.exe" "AstraDB Server"
        "https://github.com/caomengxuan666/AstraDB" "AstraDB GitHub")
    
    # Windows requires .exe extension
    set(CPACK_PACKAGE_FILE_NAME "astradb-${PROJECT_VERSION}-windows-${CMAKE_SYSTEM_PROCESSOR}")
    
elseif(APPLE)
    # macOS-specific
    set(CPACK_GENERATOR "TGZ;DragNDrop")
    set(CPACK_DMG_FORMAT "UDBZ")
    set(CPACK_DMG_VOLUME_NAME "AstraDB ${PROJECT_VERSION}")
    set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_SOURCE_DIR}/packaging/macos/background.png")
endif()

# Common settings
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0)
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_INSTALL_PREFIX "/opt/astradb")
set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/astradb")

# Package output directory
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_SOURCE_DIR}/dist")

# Enable CPack
include(CPack)
