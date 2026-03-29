# ==============================================================================
# Target Configuration
# ==============================================================================
# This module configures the main executable target with dependencies
# ==============================================================================

# ==============================================================================
# Source Files
# ==============================================================================

# Collect source files - handle both initial and full project structure
file(
  GLOB_RECURSE
  ASTRADB_SOURCES
  "src/*.cpp"
  "src/core/*.cpp"
  "src/network/*.cpp"
  "src/storage/*.cpp"
  "src/cluster/*.cpp"
  "src/protocol/*.cpp"
  "src/server/*.cpp"
  "src/utils/*.cpp")

file(
  GLOB_RECURSE
  ASTRADB_HEADERS
  "src/*.h"
  "src/*.hpp"
  "src/core/*.h"
  "src/network/*.h"
  "src/storage/*.h"
  "src/cluster/*.h"
  "src/protocol/*.h"
  "src/server/*.h"
  "src/utils/*.h"
  "src/core/*.hpp"
  "src/network/*.hpp"
  "src/storage/*.hpp"
  "src/cluster/*.hpp"
  "src/protocol/*.hpp"
  "src/server/*.hpp"
  "src/utils/*.hpp")

# ==============================================================================
# Main Executable
# ==============================================================================

# Note: The main executable is now defined in the root CMakeLists.txt
# to avoid duplicate target issues
# add_executable(astradb ${ASTRADB_SOURCES} ${ASTRADB_HEADERS})

# ==============================================================================
# Include Directories
# ==============================================================================

target_include_directories(astradb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
                                          ${CMAKE_CURRENT_BINARY_DIR})

# Add include directories for dependencies that don't provide targets
if(asio_ADDED)
  target_include_directories(astradb PRIVATE ${asio_SOURCE_DIR}/asio/include)
endif()

if(libgossip_ADDED)
  target_include_directories(astradb PRIVATE ${libgossip_SOURCE_DIR}/include)
endif()

if(flatbuffers_ADDED)
  target_include_directories(astradb PRIVATE ${flatbuffers_SOURCE_DIR}/include)
endif()

# ==============================================================================
# Link Libraries
# ==============================================================================

# Link mimalloc for high-performance memory allocation
if(TARGET mimalloc-static)
  target_link_libraries(astradb PUBLIC mimalloc-static)
  message(STATUS "Linked mimalloc-static for high-performance memory allocation")
elseif(TARGET mimalloc)
  target_link_libraries(astradb PUBLIC mimalloc)
  message(STATUS "Linked mimalloc for high-performance memory allocation")
endif()

# Configure static linking flags
if(ASTRADB_STATIC_BUILD)
  if(MSVC)
    # MSVC static linking
    target_compile_definitions(astradb PRIVATE MI_STATIC_LIB)
  else()
    # GCC/Clang static linking
    target_link_options(astradb PRIVATE
      -static
      -static-libgcc
      -static-libstdc++
    )
    target_compile_definitions(astradb PRIVATE MI_STATIC_LIB)
  endif()
endif()

# Use target-based linking for dependencies that provide targets
if(TARGET fmt::fmt)
  target_link_libraries(astradb PUBLIC fmt::fmt)
endif()

if(TARGET spdlog::spdlog)
  target_link_libraries(astradb PUBLIC spdlog::spdlog)
endif()

if(TARGET flatbuffers::flatbuffers)
  target_link_libraries(astradb PUBLIC flatbuffers::flatbuffers)
endif()

if(TARGET GTest::gtest)
  target_link_libraries(astradb PUBLIC GTest::gtest)
endif()

if(TARGET benchmark::benchmark)
  target_link_libraries(astradb PUBLIC benchmark::benchmark)
endif()

# Link libraries - only link if the dependency is available
if(TARGET asio::asio)
  target_link_libraries(astradb PUBLIC asio::asio)
  if(MSVC)
    # MSVC doesn't need -Wno-error
  else()
    target_compile_options(asio::asio PRIVATE -Wno-error)
  endif()
  target_compile_definitions(asio::asio PRIVATE ASIO_DISABLE_SEH)
endif()

if(TARGET spdlog::spdlog)
  target_link_libraries(astradb PUBLIC spdlog::spdlog)
endif()

if(TARGET libgossip::libgossip)
  target_link_libraries(astradb PUBLIC libgossip::libgossip)
endif()

if(TARGET flatbuffers::flatbuffers)
  target_link_libraries(astradb PUBLIC flatbuffers::flatbuffers)
endif()

# ==============================================================================
# Compile Options
# ==============================================================================

# Performance optimizations for static builds
if(ASTRADB_STATIC_BUILD AND CMAKE_BUILD_TYPE STREQUAL "Release")
  if(MSVC)
    # MSVC optimizations
    target_compile_options(astradb PRIVATE /O2 /GL)
  else()
    # GCC/Clang optimizations
    target_compile_options(astradb PRIVATE
      -O3
      -march=native
      -DNDEBUG
    )
  endif()
endif()

# Also disable -Werror in global flags
if(NOT MSVC)
  string(REPLACE "-Werror" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  # Disable -Werror for astradb (put at end to override other options)
  target_compile_options(astradb PRIVATE -Wno-error)
endif()

# ==============================================================================
# Precompiled Headers
# ==============================================================================

if(TARGET cotire)
  set_target_properties(astradb PROPERTIES COTIRE_ENABLE_PRECOMPILED_HEADER TRUE
                                           COTIRE_ADD_UNITY_BUILD TRUE)
endif()

# ==============================================================================
# Installation
# ==============================================================================

# Don't install anything automatically - we control installation in CMakeLists.txt
# This prevents installing third-party dependencies and headers

# Install headers only in development mode
if(ASTRADB_BUILD_DEV_PACKAGE)
  install(
    DIRECTORY src/
    DESTINATION include/astradb
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp")
endif()