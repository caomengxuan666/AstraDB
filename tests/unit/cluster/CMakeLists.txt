# ==============================================================================
# Cluster Unit Tests
# ==============================================================================

target_sources(astradb_tests PRIVATE
  shard_manager_test.cpp
  cluster_flatbuffers_test.cpp
)

target_include_directories(astradb_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)
