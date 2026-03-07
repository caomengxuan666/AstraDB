# ==============================================================================
# astra_persistence - Persistence layer (header-only)
# ==============================================================================

add_library(astra_persistence INTERFACE)

target_include_directories(astra_persistence INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Include FlatBuffers BuildFlatBuffers module
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../.cpm-cache/flatbuffers/2c40/CMake/BuildFlatBuffers.cmake")
  include(${CMAKE_CURRENT_SOURCE_DIR}/../../.cpm-cache/flatbuffers/2c40/CMake/BuildFlatBuffers.cmake)
endif()

# Generate FlatBuffers headers for RDB schema
if(flatbuffers_ADDED OR TARGET flatbuffers::flatbuffers OR TARGET flatbuffers::flatc)
  # Use official FlatBuffers CMake function
  set(RDB_FBS ${CMAKE_CURRENT_SOURCE_DIR}/rdb.fbs)
  set(RDB_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
  
  # Ensure generated directory exists
  file(MAKE_DIRECTORY ${RDB_GENERATED_DIR})
  
  build_flatbuffers(
    "${RDB_FBS}"
    ""  # No include dirs
    "rdb_generated"
    ""  # No additional dependencies
    "${RDB_GENERATED_DIR}"
    ""  # No binary schemas
    ""  # No text schema copy
  )
  
  # Make generated headers available
  target_include_directories(astra_persistence INTERFACE ${RDB_GENERATED_DIR})
  add_dependencies(astra_persistence rdb_generated)
  
  # Add FlatBuffers include directory
  target_include_directories(astra_persistence INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../../../.cpm-cache/flatbuffers/2c40/include)
  
  message(STATUS "FlatBuffers RDB headers will be generated in ${RDB_GENERATED_DIR}")
  
  # Generate FlatBuffers headers for AOF schema
  set(AOF_FBS ${CMAKE_CURRENT_SOURCE_DIR}/aof.fbs)
  
  build_flatbuffers(
    "${AOF_FBS}"
    ""  # No include dirs
    "aof_generated"
    ""  # No additional dependencies
    "${RDB_GENERATED_DIR}"
    ""  # No binary schemas
    ""  # No text schema copy
  )
  
  add_dependencies(astra_persistence aof_generated)
  
  message(STATUS "FlatBuffers AOF headers will be generated in ${RDB_GENERATED_DIR}")
endif()

# Link dependencies
target_link_libraries(astra_persistence INTERFACE
  astra_container
  astra_base
  leveldb
  zstd
  absl::strings
  absl::time
  absl::flat_hash_map
  spdlog::spdlog
)

# Platform-specific settings
if(WIN32)
  target_compile_definitions(astra_persistence INTERFACE
    _WIN32_WINNT=0x0601
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
endif()
