# ==============================================================================
# astra_server - Server core
# ==============================================================================

add_library(astra_server STATIC)

target_include_directories(astra_server PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_sources(astra_server PRIVATE
  server.cpp
  shard.cpp
)

target_link_libraries(astra_server PUBLIC
  astra_commands_whole
  astra_network
  astra_base
  astra_core
  astra_protocol
)

# Link asio if available
if(TARGET asio::asio)
  target_link_libraries(astra_server PUBLIC asio::asio)
elseif(TARGET asio)
  target_link_libraries(astra_server PUBLIC asio)
endif()