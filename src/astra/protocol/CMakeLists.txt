# ==============================================================================
# Protocol Library - RESP2/RESP3 Protocol Parser and Builder
# ==============================================================================

if(NOT TARGET astra_protocol)
  add_library(astra_protocol INTERFACE)
  
  target_include_directories(astra_protocol INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  
  target_link_libraries(astra_protocol INTERFACE
    absl::base
    absl::strings
  )
endif()

# Add protocol subdirectories
add_subdirectory(resp)