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
option(ASTRADB_BUILD_EXECUTABLE "Build main executable" ON)
option(ASTRADB_BUILD_DOCS "Build documentation" ON)
option(ASTRADB_ENABLE_COVERAGE "Enable code coverage" OFF)
option(ASTRADB_ENABLE_SANITIZERS "Enable sanitizers (debug mode)" OFF)
option(ASTRADB_ENABLE_LTO "Enable Link Time Optimization (release mode)" ON)

# ==============================================================================
# Build Configuration: Static vs Dynamic
# ==============================================================================

# Static linking options:
# - FULL_STATIC: Fully static linking (requires musl libc or glibc with --enable-static-nss)
# - PARTIAL_STATIC: Static linking except glibc (recommended for production)
# - DYNAMIC: Dynamic linking (for development)
set(ASTRADB_LINK_MODE "PARTIAL_STATIC" CACHE STRING "Linking mode: FULL_STATIC, PARTIAL_STATIC, or DYNAMIC")
set_property(CACHE ASTRADB_LINK_MODE PROPERTY STRINGS "FULL_STATIC" "PARTIAL_STATIC" "DYNAMIC")

if(ASTRADB_LINK_MODE STREQUAL "FULL_STATIC")
  message(STATUS "Linking mode: FULL_STATIC (requires musl libc or custom glibc)")
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
  add_compile_definitions(ASTRABI_FULL_STATIC)
  # Fully static linking with glibc requires special handling
  if(UNIX AND NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
  endif()
elseif(ASTRADB_LINK_MODE STREQUAL "PARTIAL_STATIC")
  message(STATUS "Linking mode: PARTIAL_STATIC (recommended for production)")
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
  # Link glibc dynamically, everything else statically
  if(UNIX AND NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-whole-archive")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--as-needed")
    # Explicitly link libc dynamically
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-whole-archive")
  endif()
else()  # DYNAMIC
  message(STATUS "Linking mode: DYNAMIC (for development)")
  set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries" FORCE)
endif()

# ==============================================================================
# C++ Standard Configuration
# ==============================================================================

# Use C++23 for AstraDB to leverage coroutines, std::generator, and other modern features
# Required for:
# - std::generator for coroutine-based iteration
# - Enhanced coroutine support with asio::awaitable
# - Improved constexpr and template metaprogramming
# - Better SIMD support with standard library improvements
set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# Warning Levels (Google C++ Style Guide)
# ==============================================================================

# Google C++ Style Guide: Use high warning levels
if(MSVC)
  add_compile_options(/W4)
  add_compile_options(/wd4100)  # unreferenced formal parameter
  add_compile_options(/wd4201)  # nameless struct/union
  add_compile_options(/wd4324)  # structure was padded due to alignment specifier
  add_compile_options(/wd4702)  # unreachable code
else()
  # GCC/Clang warnings (Google C++ Style Guide level)
  add_compile_options(-Wall -Wextra)
  add_compile_options(-Wno-unused-parameter)
  add_compile_options(-Wno-missing-field-initializers)
  add_compile_options(-Wno-sign-compare)
  
  # Disable pedantic warnings for Abseil and third-party library compatibility
  # Abseil uses __int128 and other GCC extensions which are not ISO C++
  add_compile_options(-Wno-pedantic)
  
  # Disable stringop-overflow warnings for third-party libraries
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-error=stringop-overflow)
endif()
  
  # Note: -Werror disabled due to third-party library compatibility
  # Enable selectively for production code
endif()

# ==============================================================================
# Output Directory Configuration
# ==============================================================================

# Organize build artifacts by type and configuration (Google C++ Style Guide)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Per-configuration output directories
foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
  string(TOUPPER ${CONFIG} CONFIG_UPPER)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/bin/${CONFIG})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/lib/${CONFIG})
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/lib/${CONFIG})
endforeach()

# ==============================================================================
# C++ Standard Policy Version Minimum
# ==============================================================================

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# ==============================================================================
# Export compile commands for IDEs
# ==============================================================================

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)