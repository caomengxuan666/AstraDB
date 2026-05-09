# ==============================================================================
# Command Tests
# ==============================================================================

option(ASTRADB_ENABLE_LEGACY_COMMAND_UNIT_TESTS
  "Enable legacy command unit tests that are currently disabled"
  OFF)

# Add test sources to parent test executable
target_sources(astradb_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/ttl_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/script_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stream_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/transaction_test.cpp
)

if(ASTRADB_ENABLE_LEGACY_COMMAND_UNIT_TESTS)
  target_sources(astradb_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/command_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hello_test.cpp
  )
endif()


if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_link_options(astradb_tests PRIVATE 
    -Wl,--no-fatal-warnings
    -Wl,--no-warn-search-mismatch
  )
endif()

# Link additional libraries needed for command tests
target_link_libraries(astradb_tests PRIVATE
  astra_commands_whole
  lua::lua
)
