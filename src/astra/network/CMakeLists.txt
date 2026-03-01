# ==============================================================================
# Network Layer - Asio-based async network handling
# ==============================================================================

add_library(astra_network
  connection.cpp
)

target_include_directories(astra_network PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(astra_network PUBLIC
  absl::base
  astra_protocol_resp
  spdlog::spdlog
)

target_compile_features(astra_network PUBLIC cxx_std_23)

# Find Asio
if(asio_SOURCE_DIR)
  target_include_directories(astra_network PRIVATE ${asio_SOURCE_DIR}/asio/include)
  target_link_libraries(astra_network PRIVATE asio)
endif()