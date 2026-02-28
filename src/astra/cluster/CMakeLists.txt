# ==============================================================================
# astra_cluster - Cluster management
# ==============================================================================

add_library(astra_cluster INTERFACE)

target_include_directories(astra_cluster INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(astra_cluster INTERFACE
  astra_server
)

# Link libgossip if available
if(TARGET libgossip)
  target_link_libraries(astra_cluster INTERFACE libgossip)
endif()
if(TARGET libgossip_net)
  target_link_libraries(astra_cluster INTERFACE libgossip_net)
endif()