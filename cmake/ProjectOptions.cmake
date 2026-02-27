# ==============================================================================
# Project Build Options
# ==============================================================================
# This module defines project-wide build options and settings
# ==============================================================================

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
# Project Options
# ==============================================================================

option(ASTRADB_BUILD_TESTS "Build unit tests" ON)
option(ASTRADB_BUILD_BENCHMARKS "Build performance benchmarks" ON)
option(ASTRADB_BUILD_DOCS "Build documentation" ON)
option(ASTRADB_ENABLE_COVERAGE "Enable code coverage" OFF)
option(ASTRADB_ENABLE_SANITIZERS "Enable sanitizers (debug mode)" OFF)
option(ASTRADB_ENABLE_LTO "Enable Link Time Optimization (release mode)" ON)

# ==============================================================================
# C++ Standard Configuration
# ==============================================================================

set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# Warning Levels
# ==============================================================================

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  # Note: -Werror disabled due to C++20 compatibility issues with third-party libraries
endif()

# ==============================================================================
# C++ Standard Policy Version Minimum
# ==============================================================================

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)