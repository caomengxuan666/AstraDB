# ==============================================================================
# Persistence Unit Tests
# ==============================================================================

target_sources(astradb_tests PRIVATE
  aof_test.cpp
  aof_flatbuffers_test.cpp
  rdb_test.cpp
)

target_include_directories(astradb_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../..
  ${CMAKE_BINARY_DIR}/src/astra/persistence/generated
)
