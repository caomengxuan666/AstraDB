# ==============================================================================
# astra_server - Server core
# ==============================================================================

add_library(astra_server INTERFACE)

target_include_directories(astra_server INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(astra_server INTERFACE
  astra_commands
)

# Link asio if available
if(TARGET asio::asio)
  target_link_libraries(astra_server INTERFACE asio::asio)
elseif(TARGET asio)
  target_link_libraries(astra_server INTERFACE asio)
endif()