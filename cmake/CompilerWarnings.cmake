# ==============================================================================
# Compiler-Specific Warnings Configuration
# ==============================================================================
# This module configures compiler warnings for different compilers
# ==============================================================================

# Suppress C++23 deprecation warnings for compatibility
add_compile_definitions(_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING)
add_compile_definitions(_SILENCE_CXX23_DENORM_DEPRECATION_WARNING)

if(MSVC)
  # MSVC: Don't treat warnings as errors by default
  # /W4 enables high warning level
  message(STATUS "🛡️  Configured MSVC warnings (not treating warnings as errors)")
else()
  # GCC/Clang: Disable -Werror globally
  add_compile_options(-Wno-error)
  string(REPLACE "-Werror" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REPLACE "-Werror" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REPLACE "-Werror" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  message(STATUS "🛡️  Completely disabled -Werror for GCC/Clang compiler")
endif()