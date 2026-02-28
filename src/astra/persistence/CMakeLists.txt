# ==============================================================================
# astra_persistence - Persistence layer
# ==============================================================================

add_library(astra_persistence INTERFACE)

target_include_directories(astra_persistence INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(astra_persistence INTERFACE
  astra_engine
)