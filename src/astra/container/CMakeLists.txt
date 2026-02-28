# ==============================================================================
# astra_container - Data structures
# ==============================================================================

add_library(astra_container INTERFACE)

target_include_directories(astra_container INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(astra_container INTERFACE
  astra_core
)