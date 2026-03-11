# ==============================================================================
# io_uring Debug Examples
# ==============================================================================

cmake_minimum_required(VERSION 3.20)
project(io_uring_examples CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# Check for io_uring support
# ==============================================================================

message(STATUS "Checking for io_uring support...")

include(CheckIncludeFileCXX)
check_include_file_cxx("linux/io_uring.h" HAVE_IO_URING_H)

if(NOT HAVE_IO_URING_H)
  message(WARNING "io_uring headers not found")
  return()
endif()

# Check kernel version (io_uring requires kernel 5.1+)
find_program(UNAME_CMD uname)
if(UNAME_CMD)
  execute_process(
    COMMAND ${UNAME_CMD} -r
    OUTPUT_VARIABLE KERNEL_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  message(STATUS "Kernel version: ${KERNEL_VERSION}")
  
  # Simple version check: 5.1+
  # This is a basic check, may need refinement
  if(KERNEL_VERSION VERSION_LESS "5.1")
    message(WARNING "Kernel version ${KERNEL_VERSION} is too old for io_uring (requires 5.1+)")
    return()
  endif()
endif()

# Check if io_uring is enabled in kernel
execute_process(
  COMMAND cat /proc/sys/kernel/io_uring_enabled
  OUTPUT_VARIABLE IO_URING_ENABLED
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

if(IO_URING_ENABLED STREQUAL "0")
  message(WARNING "io_uring is disabled in kernel (io_uring_enabled=0)")
  return()
endif()

message(STATUS "io_uring is available")

# ==============================================================================
# Find liburing
# ==============================================================================

find_path(LIBURING_INCLUDE_DIR NAMES liburing.h PATHS /usr/include /usr/local/include)
find_library(LIBURING_LIBRARY NAMES uring PATHS /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64)

if(NOT LIBURING_INCLUDE_DIR OR NOT LIBURING_LIBRARY)
  message(WARNING "liburing not found")
  return()
endif()

message(STATUS "Found liburing: ${LIBURING_LIBRARY}")

# ==============================================================================
# Add ASIO dependency
# ==============================================================================

if(NOT TARGET asio)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../.cpm-cache/asio/5c6a/asio/include")
    add_library(asio INTERFACE)
    target_include_directories(asio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../../.cpm-cache/asio/5c6a/asio/include)
  endif()
endif()

# ==============================================================================
# Example 1: Raw io_uring Server
# ==============================================================================

add_executable(raw_io_uring_server raw_io_uring_server.cpp)
target_link_libraries(raw_io_uring_server ${LIBURING_LIBRARY})
target_include_directories(raw_io_uring_server PRIVATE ${LIBURING_INCLUDE_DIR})

set_target_properties(raw_io_uring_server PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

message(STATUS "Added target: raw_io_uring_server")

# ==============================================================================
# Example 2: ASIO + io_uring Server
# ==============================================================================

if(TARGET asio)
  add_executable(asio_io_uring_server asio_io_uring_server.cpp)
  target_link_libraries(asio_io_uring_server asio pthread ${LIBURING_LIBRARY})
  target_compile_definitions(asio_io_uring_server PRIVATE ASIO_HAS_IO_URING=1 ASIO_DISABLE_EPOLL=1)
  set_target_properties(asio_io_uring_server PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
  message(STATUS "Added target: asio_io_uring_server")
endif()

# ==============================================================================
# Example 3: Test Client
# ==============================================================================

if(TARGET asio)
  add_executable(test_client test_client.cpp)
  target_link_libraries(test_client asio pthread)
  set_target_properties(test_client PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
  message(STATUS "Added target: test_client")
endif()

# ==============================================================================
# Testing targets
# ==============================================================================

if(TARGET raw_io_uring_server AND TARGET test_client)
  add_custom_target(test_raw_io_uring
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/raw_io_uring_server 8765 &
    COMMAND sleep 2
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_client -p 8765 -n 5
    DEPENDS raw_io_uring_server test_client
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Testing raw io_uring server..."
  )
endif()

if(TARGET asio_io_uring_server AND TARGET test_client)
  add_custom_target(test_asio_io_uring
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/asio_io_uring_server 8766 &
    COMMAND sleep 2
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_client -p 8766 -n 5
    DEPENDS asio_io_uring_server test_client
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Testing ASIO io_uring server..."
  )
endif()

if(TARGET test_raw_io_uring AND TARGET test_asio_io_uring)
  add_custom_target(test_all_io_uring DEPENDS test_raw_io_uring test_asio_io_uring)
endif()
