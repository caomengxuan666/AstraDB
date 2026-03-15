# ==============================================================================
# Platform-Specific Settings
# ==============================================================================
# This module configures platform-specific settings and definitions
# ==============================================================================

# Find threads library first (required for macOS and Linux)
if(NOT WIN32)
  find_package(Threads REQUIRED)
endif()

if(WIN32)
  # Windows-specific settings
  add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -D_WIN32_WINNT=0x0601)
  
  # Windows-specific link libraries
  # pthread is not needed on Windows, use native threading
  # Networking libraries
  set(PLATFORM_LIBRARIES ws2_32 Winmm iphlpapi)
  
  message(STATUS "Platform: Windows")
  message(STATUS "Platform libraries: ${PLATFORM_LIBRARIES}")
  
elseif(APPLE)
  # macOS-specific settings
  # macOS uses pthreads by default
  set(PLATFORM_LIBRARIES Threads::Threads)
  
  message(STATUS "Platform: macOS")
  message(STATUS "Platform libraries: ${PLATFORM_LIBRARIES}")
  
elseif(UNIX)
  # Linux-specific settings
  # Linux requires pthread for threading support
  set(PLATFORM_LIBRARIES Threads::Threads rt)

# Check io_uring support for Asio (Linux 5.1+)
  # Only enabled if ASTRADB_ENABLE_IO_URING is ON
  if(ASTRADB_ENABLE_IO_URING)
    find_library(LIBURING_LIB NAMES uring)
    if(LIBURING_LIB)
      message(STATUS "  io_uring support: Enabled (using liburing)")
      set(ASTRADB_IO_URING_ENABLED "ON")
    else()
      message(STATUS "  io_uring support: Disabled (liburing not found)")
      set(ASTRADB_IO_URING_ENABLED "OFF")
    endif()
  else()
    message(STATUS "  io_uring support: Disabled (ASTRADB_ENABLE_IO_URING=OFF)")
    set(ASTRADB_IO_URING_ENABLED "OFF")
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