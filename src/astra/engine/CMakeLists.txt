# ==============================================================================
# astra_engine - Storage engine
# ==============================================================================

add_library(astra_engine INTERFACE)

target_include_directories(astra_engine INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(astra_engine INTERFACE
  astra_container
  astra_base
  leveldb
  zstd
)