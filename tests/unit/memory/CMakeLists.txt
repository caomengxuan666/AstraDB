# ==============================================================================
# Memory Unit Tests
# ==============================================================================

# Add test sources to parent test executable
target_sources(astradb_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/eviction_policy_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/memory_tracker_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/eviction_manager_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/object_size_estimator_test.cpp
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_link_options(astradb_tests PRIVATE
    -Wl,--no-fatal-warnings
    -Wl,--no-warn-search-mismatch
  )
endif()