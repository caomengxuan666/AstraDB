# ==============================================================================
# Platform-Specific Settings
# ==============================================================================
# This module configures platform-specific settings and definitions
# ==============================================================================

if(WIN32)
  # Windows-specific settings
  add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
  
  # Windows-specific link libraries
  # pthread is not needed on Windows, use native threading
  # Networking libraries
  set(PLATFORM_LIBRARIES ws2_32 Winmm iphlpapi)
  
  message(STATUS "Platform: Windows")
  message(STATUS "Platform libraries: ${PLATFORM_LIBRARIES}")
  
elseif(APPLE)
  # macOS-specific settings
  # macOS uses pthreads by default
  set(PLATFORM_LIBRARIES pthread)
  
  message(STATUS "Platform: macOS")
  message(STATUS "Platform libraries: ${PLATFORM_LIBRARIES}")
  
elseif(UNIX)
  # Linux-specific settings
  # Linux requires pthread for threading support
  set(PLATFORM_LIBRARIES pthread rt)
  
  # Enable io_uring support for Asio (Linux 5.1+)
  # Check if io_uring is available
  set(IO_URING_TEST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/io_uring_test.c")
  if(EXISTS "${IO_URING_TEST_SOURCE}")
    try_compile(HAVE_IO_URING
      "${CMAKE_CURRENT_BINARY_DIR}/io_uring_test"
      "${IO_URING_TEST_SOURCE}"
      COMPILE_DEFINITIONS "-DASIO_HAS_IO_URING=1"
      LINK_LIBRARIES "pthread;rt"
    )
    
    if(HAVE_IO_URING)
      message(STATUS "  io_uring support: Enabled")
      add_compile_definitions(ASIO_HAS_IO_URING=1)
    else()
      message(STATUS "  io_uring support: Not available (requires Linux 5.1+)")
    endif()
  else()
    message(STATUS "  io_uring test file not found, skipping io_uring check")
  endif()
  
  # Handle static linking on Linux to avoid glibc NSS warnings
  if(ASTRADB_LINK_MODE STREQUAL "PARTIAL_STATIC")
    # In partial static mode, link glibc dynamically to avoid NSS warnings
    # This means static linking everything except glibc
    message(STATUS "Building with PARTIAL_STATIC on Linux (glibc dynamic)")
    
    # Don't force -static, but link libraries statically
    # glibc and NSS will be linked dynamically
    add_link_options(-Wl,--as-needed)
    
  elseif(ASTRADB_LINK_MODE STREQUAL "FULL_STATIC")
    # Fully static - user must ensure glibc is built with --enable-static-nss
    # or use musl libc
    message(WARNING "FULL_STATIC on Linux requires musl libc or glibc with --enable-static-nss")
    message(WARNING "NSS warnings may occur if using standard glibc")
    add_link_options(-Wl,--no-as-needed)
    
  else()
    # Dynamic linking - default
    add_link_options(-Wl,--as-needed)
  endif()
  
  message(STATUS "Platform: Linux")
  message(STATUS "Platform libraries: ${PLATFORM_LIBRARIES}")
  message(STATUS "Link mode: ${ASTRADB_LINK_MODE}")
  
endif()