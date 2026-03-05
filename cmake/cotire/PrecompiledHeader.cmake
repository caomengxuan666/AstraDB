# ==============================================================================
# Precompiled Header (PCH) Configuration
# ==============================================================================
# This module configures cotire for creating and using precompiled headers
# to accelerate compilation times.
# ==============================================================================

# Include cotire module
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/cotire.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/cotire.cmake")
endif()

if(NOT CMAKE_VERSION VERSION_LESS 3.16)
    # Enable cotire for the project
    option(ENABLE_PCH "Enable Precompiled Headers for faster compilation" ON)
    
    if(ENABLE_PCH)
        # Common PCH settings for all targets
        set(COTIRE_ADD_PREFIX_HEADER_ADDITION TRUE)
        set(COTIRE_UNITY_SOURCE FALSE)  # Disable unity build, only use PCH
        
        # Exclude some targets from PCH (test targets, etc.)
        set(COTIRE_EXCLUDE_TARGET_PATTERN "_test$|_bench$")
        
        # Maximum number of source files to combine into a single unity file
        set(COTIRE_MINIMUM_NUMBER_OF_TARGET_SOURCES 2)
        
        message(STATUS "Precompiled Headers enabled for faster compilation (unity build disabled)")
    endif()
endif()