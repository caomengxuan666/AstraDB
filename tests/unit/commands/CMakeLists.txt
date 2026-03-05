# ==============================================================================
# Command Tests
# ==============================================================================

# Add test sources to parent test executable
target_sources(astradb_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/command_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ttl_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/script_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stream_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/transaction_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/multi_database_test.cpp
)

# Link additional libraries needed for command tests
target_link_libraries(astradb_tests PRIVATE
  astra_commands_whole
  lua::lua
)