# ==============================================================================
# AstraDB - High-Performance Redis-Compatible Database
# ==============================================================================
# This is a complete rewrite designed to exceed DragonflyDB performance
# Target: 2x DragonflyDB performance, 50% less memory usage
# ==============================================================================

cmake_minimum_required(VERSION 3.20)
project(
  AstraDB
  VERSION 1.0.0
  LANGUAGES CXX)

# ==============================================================================
# Load Modular CMake Configuration
# ==============================================================================

# Include all configuration modules
include(cmake/EnvLoader.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/CPMConfig.cmake)
include(cmake/PlatformSettings.cmake)
include(cmake/ProjectOptions.cmake)

# ==============================================================================
# Initialize Environment
# ==============================================================================

# Load .env file
read_env_file()

# ==============================================================================
# CMakeHub Integration
# ==============================================================================

# Set CMakeHub cache directory
set(CMH_CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.cmakehub/cache")

# Load CMakeHub modules
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/hub/loader.cmake)

# Note: Temporarily disabled CMakeHub modules due to compatibility issues
# Uncomment these when needed:
cmakehub_use(precompiled_header)
cmakehub_use(lto_optimization)
cmakehub_use(compile_options)
cmakehub_use(compiler_warnings)
cmakehub_use(clang_tidy_tools)
cmakehub_use(code_formatter)
# cmakehub_use(sanitizers)

# Load CPM from CMakeHub
cmakehub_use(cpm)

# ==============================================================================
# Third-Party Dependencies
# ==============================================================================

include(cmake/Dependencies.cmake)

# ==============================================================================
# Documentation and Tools
# ==============================================================================

# Doxygen Documentation Generation
cmakehub_use(doxygen_helper)

# Git Version Management
cmakehub_use(git_version)

# ==============================================================================
# Target Configuration
# ==============================================================================

include(cmake/TargetConfig.cmake)

# ==============================================================================
# Testing (Temporarily Disabled)
# ==============================================================================

# Note: Temporarily disabled until test infrastructure is set up
# if(ASTRADB_BUILD_TESTS)
#   enable_testing()
#   add_executable(astradb_tests tests/unit/*.cpp)
#   target_include_directories(astradb_tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
#   if(TARGET GTest::gtest)
#     target_link_libraries(astradb_tests PRIVATE GTest::gtest)
#   endif()
#   add_test(NAME astradb_unit_tests COMMAND astradb_tests)
# endif()

# ==============================================================================
# Documentation (Temporarily Disabled)
# ==============================================================================

# Note: Temporarily disabled until docs directory is created
# if(ASTRADB_BUILD_DOCS)
#   add_subdirectory(docs)
# endif()

# ==============================================================================
# Configuration Summary
# ==============================================================================

include(cmake/ConfigSummary.cmake)