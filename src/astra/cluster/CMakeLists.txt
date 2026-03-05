# ==============================================================================
# astra_cluster - Cluster management with libgossip (header-only)
# ==============================================================================

add_library(astra_cluster INTERFACE)

target_include_directories(astra_cluster INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Generate FlatBuffers headers for cluster communication
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/../../../bin/flatc")
  set(FLATC_EXECUTABLE "${CMAKE_CURRENT_BINARY_DIR}/../../../bin/flatc")
  set(CLUSTER_MESSAGE_FBS ${CMAKE_CURRENT_SOURCE_DIR}/cluster_message.fbs)
  set(CLUSTER_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
  
  # Create generated directory
  file(MAKE_DIRECTORY ${CLUSTER_GENERATED_DIR})
  
  # Generate FlatBuffers headers using add_custom_command
  add_custom_command(
    OUTPUT ${CLUSTER_GENERATED_DIR}/cluster_message_generated.h
    COMMAND ${FLATC_EXECUTABLE} --gen-mutable -o ${CLUSTER_GENERATED_DIR} -c ${CLUSTER_MESSAGE_FBS}
    DEPENDS ${CLUSTER_MESSAGE_FBS}
    COMMENT "Generating FlatBuffers headers for cluster communication"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  
  # Create custom target for the generated header
  add_custom_target(cluster_message_generated
    DEPENDS ${CLUSTER_GENERATED_DIR}/cluster_message_generated.h
  )
  
  # Add generated directory to include paths
  target_include_directories(astra_cluster INTERFACE ${CLUSTER_GENERATED_DIR})
  
  # Add FlatBuffers include directory
  target_include_directories(astra_cluster INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../../../.cpm-cache/flatbuffers/2c40/include)
  
  # Make astra_cluster depend on the generated header
  add_dependencies(astra_cluster cluster_message_generated)
  
  message(STATUS "✅ FlatBuffers cluster headers will be generated in ${CLUSTER_GENERATED_DIR}")
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
  # Add libgossip's third_party includes (magic_enum, nlohmann_json, etc.)
  # Note: libgossip uses #include "magic_enum/magic_enum.hpp", so we need the parent dir
  if(DEFINED libgossip_download_SOURCE_DIR)
    target_include_directories(astra_cluster INTERFACE
      ${libgossip_download_SOURCE_DIR}/third_party
      ${libgossip_download_SOURCE_DIR}/third_party/json/single_include
    )
  elseif(DEFINED LIBGOSSIP_SOURCE)
    target_include_directories(astra_cluster INTERFACE
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
    _WIN32_WINNT=0x0601
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
endif()