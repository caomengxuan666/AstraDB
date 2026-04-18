# ==============================================================================
# Configuration Summary
# ==============================================================================
# This module prints a summary of the build configuration
# ==============================================================================

message(STATUS "")
message(
  STATUS
    "==============================================================================="
)
message(STATUS "AstraDB Configuration Summary")
message(
  STATUS
    "==============================================================================="
)
message(STATUS "Version:             ${PROJECT_VERSION}")
message(STATUS "Build Type:          ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:        ${CMAKE_CXX_STANDARD}")
message(
  STATUS
    "Compiler:            ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)
message(STATUS "Build Tests:         ${ASTRADB_BUILD_TESTS}")
message(STATUS "Build Benchmarks:    ${ASTRADB_BUILD_BENCHMARKS}")
message(STATUS "Build Docs:          ${ASTRADB_BUILD_DOCS}")
message(STATUS "Enable Coverage:     ${ASTRADB_ENABLE_COVERAGE}")
message(STATUS "Enable Sanitizers:   ${ASTRADB_ENABLE_SANITIZERS}")
message(STATUS "Enable LTO:          ${ASTRADB_ENABLE_LTO}")
message(STATUS "io_uring Support:    ${ASTRADB_IO_URING_ENABLED}")
if(ASTRADB_RELEASE_OPTIMIZED)
  message(STATUS "Release Optimized:   YES (TRACE logs disabled at compile-time)")
else()
  message(STATUS "Release Optimized:   NO (TRACE logs enabled)")
endif()
message(
  STATUS
    "==============================================================================="
)
message(STATUS "")