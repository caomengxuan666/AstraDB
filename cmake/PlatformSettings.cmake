# ==============================================================================
# Platform-Specific Settings
# ==============================================================================
# This module configures platform-specific settings and definitions
# ==============================================================================

if(WIN32)
  # Windows-specific settings
  add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
  message(STATUS "Platform: Windows")
elseif(APPLE)
  # macOS-specific settings
  message(STATUS "Platform: macOS")
else()
  # Linux-specific settings
  message(STATUS "Platform: Linux")
endif()