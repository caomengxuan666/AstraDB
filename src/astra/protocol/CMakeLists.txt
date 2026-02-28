# ==============================================================================
# astra_protocol - Protocol handling
# ==============================================================================

add_library(astra_protocol INTERFACE)

target_include_directories(astra_protocol INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(astra_protocol INTERFACE
  astra_core
)