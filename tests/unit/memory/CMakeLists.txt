# ==============================================================================
# Memory Unit Tests
# ==============================================================================

if(ASTRADB_BUILD_TESTS)
  target_sources(astradb_tests PRIVATE
    arena_test.cpp
  )
endif()