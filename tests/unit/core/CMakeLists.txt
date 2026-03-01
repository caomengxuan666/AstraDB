# ==============================================================================
# Core Unit Tests
# ==============================================================================

if(ASTRADB_BUILD_TESTS)
  target_sources(astradb_tests PRIVATE
    executor_test.cpp
  )
endif()