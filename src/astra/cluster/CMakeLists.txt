# ==============================================================================
# astra_cluster - Cluster management with libgossip
# ==============================================================================

add_library(astra_cluster INTERFACE)

target_include_directories(astra_cluster INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Link dependencies
target_link_libraries(astra_cluster INTERFACE
  astra_server
  absl::strings
  absl::time
  absl::container
  absl::synchronization
  spdlog::spdlog
)

# Link libgossip if available
if(TARGET libgossip)
  target_link_libraries(astra_cluster INTERFACE libgossip)
  message(STATUS "✅ libgossip linked to astra_cluster")
endif()
if(TARGET libgossip_net)
  target_link_libraries(astra_cluster INTERFACE libgossip_net)
  message(STATUS "✅ libgossip_net linked to astra_cluster")
endif()

# Platform-specific settings
if(WIN32)
  target_compile_definitions(astra_cluster INTERFACE
    _WIN32_WINNT=0x0601
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
endif()
