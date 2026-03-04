# AstraDB Replication Module

add_library(astra_replication INTERFACE)

target_include_directories(astra_replication INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(astra_replication INTERFACE
  astra_protocol
  astra_base
)