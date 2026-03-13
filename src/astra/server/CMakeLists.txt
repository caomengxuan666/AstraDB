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
  worker.hpp
)

target_link_libraries(astra_server PUBLIC
  astra_core
  astra_network
  astra_protocol_resp
  astra_commands_whole
  flatbuffers_generated
  concurrentqueue
  astra_persistence
  absl::time
  absl::flat_hash_map
)

# Link asio if available
if(TARGET asio::asio)
  target_link_libraries(astra_server PUBLIC asio::asio)
elseif(TARGET asio)
  target_link_libraries(astra_server PUBLIC asio)
endif()