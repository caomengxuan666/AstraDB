# ==============================================================================
# astra_cluster - Cluster management with libgossip (header-only)
# ==============================================================================

add_library(astra_cluster INTERFACE)

target_include_directories(astra_cluster INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
  ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# FlatBuffers headers are now generated in prebuild.cmake (MUST COMPILE FIRST!)
if(flatbuffers_ADDED OR TARGET flatbuffers::flatbuffers OR TARGET flatbuffers::flatc)
  # CRITICAL: Force flatbuffers_generated to compile BEFORE ANYTHING ELSE
  if(TARGET flatbuffers_generated)
    add_dependencies(astra_cluster flatbuffers_generated)
    target_include_directories(astra_cluster INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../../../.cpm-cache/flatbuffers/2c40/include)
  endif()
endif()

# Link dependencies
target_link_libraries(astra_cluster INTERFACE
  astra_server
  absl::strings
  absl::time
  absl::flat_hash_map
  absl::synchronization
  spdlog::spdlog
)

# Link libgossip if available (it exports PUBLIC include directories)
if(TARGET libgossip)
  target_link_libraries(astra_cluster INTERFACE libgossip)
  # Add libgossip's include directory
  if(DEFINED libgossip_download_SOURCE_DIR)
    target_include_directories(astra_cluster INTERFACE
      ${libgossip_download_SOURCE_DIR}/include
      ${libgossip_download_SOURCE_DIR}/third_party
      ${libgossip_download_SOURCE_DIR}/third_party/json/single_include
    )
  elseif(DEFINED LIBGOSSIP_SOURCE)
    target_include_directories(astra_cluster INTERFACE
      ${LIBGOSSIP_SOURCE}/include
      ${LIBGOSSIP_SOURCE}/third_party
      ${LIBGOSSIP_SOURCE}/third_party/json/single_include
    )
  endif()
  message(STATUS "✅ libgossip linked to astra_cluster")
endif()
if(TARGET libgossip_net)
  target_link_libraries(astra_cluster INTERFACE libgossip_net)
  message(STATUS "✅ libgossip_net linked to astra_cluster")
endif()

# Platform-specific settings
if(WIN32)
  target_compile_definitions(astra_cluster INTERFACE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
endif()