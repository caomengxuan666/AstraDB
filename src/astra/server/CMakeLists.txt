# ==============================================================================
# astra_server - Server core
# ==============================================================================

add_library(astra_server STATIC)

target_include_directories(astra_server PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Add libgossip's third_party directory for magic_enum headers
if(TARGET libgossip OR libgossip_SOURCE_DIR)
  target_include_directories(astra_server PUBLIC
    ${libgossip_SOURCE_DIR}/third_party
  )
endif()

target_sources(astra_server PRIVATE
  server.cpp
  server.hpp
  transaction_state.cpp
  transaction_state.hpp
  worker.cpp
  worker.hpp
  worker_scheduler.hpp
  worker_scheduler.cpp
)

target_link_libraries(astra_server PUBLIC
  astra_core
  astra_network
  astra_protocol_resp
  astra_commands_whole
  astra_cluster
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
