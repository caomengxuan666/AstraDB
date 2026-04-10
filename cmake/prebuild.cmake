# ==============================================================================
# PREBUILD - MUST COMPILE FIRST!
# ==============================================================================
# This file ensures all FlatBuffers headers are generated BEFORE any
# other compilation happens. This is included after Dependencies.cmake
# and before add_subdirectory(src/astra).
# ==============================================================================

message(STATUS "🔒 PREBUILD: Setting up FlatBuffers generation...")

# Create a custom target that will generate all FlatBuffers headers
add_custom_target(flatbuffers_prebuild ALL
  COMMAND ${CMAKE_COMMAND} -E echo "✅ FlatBuffers headers generation complete"
  COMMENT "Generating all FlatBuffers headers (MUST RUN FIRST)"
)

# Helper function to add flatbuffers generation
function(add_flatbuffers_generation FBS_FILE TARGET_NAME GENERATED_DIR)
  get_filename_component(FBS_NAME ${FBS_FILE} NAME_WE)
  
  # Generate header file
  add_custom_command(
    OUTPUT ${GENERATED_DIR}/${FBS_NAME}_generated.h
    COMMAND flatc --cpp -o ${GENERATED_DIR} ${FBS_FILE}
    DEPENDS ${FBS_FILE}
    COMMENT "Generating ${FBS_NAME}_generated.h"
    VERBATIM
  )
  
  # Create a custom target for this schema
  add_custom_target(${TARGET_NAME}
    DEPENDS ${GENERATED_DIR}/${FBS_NAME}_generated.h
  )
  
  # Make the prebuild target depend on this
  add_dependencies(flatbuffers_prebuild ${TARGET_NAME})
endfunction()

# Generate ALL FlatBuffers schemas
add_flatbuffers_generation(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/core/metrics.fbs
  metrics_generated
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/core/generated
)

add_flatbuffers_generation(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/persistence/rdb.fbs
  rdb_generated
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/persistence/generated
)

add_flatbuffers_generation(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/persistence/aof.fbs
  aof_generated
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/persistence/generated
)

add_flatbuffers_generation(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/persistence/rocksdb.fbs
  rocksdb_generated
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/persistence/generated
)

add_flatbuffers_generation(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/cluster/cluster_message.fbs
  cluster_message_generated
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/cluster/generated
)

add_flatbuffers_generation(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/commands/command_cache.fbs
  command_cache_generated
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/commands/generated
)

# Create an INTERFACE library that contains all generated headers
add_library(flatbuffers_generated INTERFACE)

target_include_directories(flatbuffers_generated INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/core/generated
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/persistence/generated
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/cluster/generated
  ${CMAKE_CURRENT_SOURCE_DIR}/src/astra/commands/generated
  ${CMAKE_CURRENT_SOURCE_DIR}/.cpm-cache/flatbuffers/2c40/include
)

# Make flatbuffers_generated depend on prebuild
add_dependencies(flatbuffers_generated flatbuffers_prebuild)

message(STATUS "✅ PREBUILD: FlatBuffers generation targets created")
message(STATUS "   Headers will be generated in source tree, better for lint tools")