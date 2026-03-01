# ==============================================================================
# Container Library - High-performance data structures
# ==============================================================================

if(NOT TARGET astra_container)
  add_library(astra_container INTERFACE)
  
  target_include_directories(astra_container INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  
  target_link_libraries(astra_container INTERFACE
    absl::base
    absl::hash
    absl::flat_hash_map
    absl::synchronization
  )
endif()