# ==============================================================================
# astra_commands - Command implementations
# ==============================================================================

add_library(astra_commands INTERFACE)

target_include_directories(astra_commands INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(astra_commands INTERFACE
  astra_engine
  astra_protocol
  lua::lua
  cxxopts::cxxopts
  tomlplusplus::tomlplusplus
)