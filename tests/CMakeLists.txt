# ==============================================================================
# Tests Configuration
# ==============================================================================

if(ASTRADB_BUILD_TESTS)
  enable_testing()
  
  # Add Google Test
  if(TARGET GTest::gtest)
    message(STATUS "Building tests with Google Test")
  endif()
  
  # Add Google Benchmark
  if(TARGET benchmark::benchmark AND ASTRADB_BUILD_BENCHMARKS)
    message(STATUS "Building benchmarks with Google Benchmark")
  endif()
endif()