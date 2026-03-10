# ==============================================================================
# astra_commands - Command implementations
# ==============================================================================

# Create separate command library for whole-archive linking
add_library(astra_commands STATIC)

# Note: UNITY BUILD disabled for astra_commands due to:
# 1. FlatBuffers generated header include path issues
# 2. Command registration uses __LINE__ macro which conflicts when files are unified
target_include_directories(astra_commands PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
  ${CMAKE_CURRENT_SOURCE_DIR}/../cluster
  ${CMAKE_CURRENT_SOURCE_DIR}/../persistence
)

# Add persistence generated directory for aof_flatbuffers.hpp
set(PERSISTENCE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/../persistence/generated")
file(MAKE_DIRECTORY ${PERSISTENCE_GENERATED_DIR})
target_include_directories(astra_commands PUBLIC ${PERSISTENCE_GENERATED_DIR})

# Add core generated directory for metrics_flatbuffers.hpp
set(CORE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/../core/generated")
file(MAKE_DIRECTORY ${CORE_GENERATED_DIR})
target_include_directories(astra_commands PUBLIC ${CORE_GENERATED_DIR})

# Include FlatBuffers BuildFlatBuffers module
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../.cpm-cache/flatbuffers/2c40/CMake/BuildFlatBuffers.cmake")
  include(${CMAKE_CURRENT_SOURCE_DIR}/../../.cpm-cache/flatbuffers/2c40/CMake/BuildFlatBuffers.cmake)
endif()

# Generate FlatBuffers headers for command cache schema
# Note: We check for flatbuffers_ADDED which is set by CPM in the root CMakeLists.txt
if(flatbuffers_ADDED OR TARGET flatbuffers::flatbuffers OR TARGET flatbuffers::flatc)
  set(COMMAND_CACHE_FBS ${CMAKE_CURRENT_SOURCE_DIR}/command_cache.fbs)
  set(COMMAND_CACHE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
  
  build_flatbuffers(
    "${COMMAND_CACHE_FBS}"
    ""  # No include dirs
    "command_cache_generated"
    ""  # No additional dependencies
    "${COMMAND_CACHE_GENERATED_DIR}"
    ""  # No binary schemas
    ""  # No text schema copy
  )
  
  target_include_directories(astra_commands PUBLIC ${COMMAND_CACHE_GENERATED_DIR})
  add_dependencies(astra_commands command_cache_generated)
  
  # Add FlatBuffers include directory
  target_include_directories(astra_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../.cpm-cache/flatbuffers/2c40/include)
  
  message(STATUS "FlatBuffers command cache headers will be generated in ${COMMAND_CACHE_GENERATED_DIR}")
endif()

# Add vog/sha1 include directory for SHA1 hashing
if(sha1_SOURCE_DIR)
  target_include_directories(astra_commands PUBLIC ${sha1_SOURCE_DIR})
endif()

# All command files (including core registration logic)
target_sources(astra_commands PRIVATE
  command_handler.cpp
  data_type_registry.cpp
  command_auto_register.cpp
  string_commands.cpp
  hash_commands.cpp
  set_commands.cpp
  zset_commands.cpp
  list_commands.cpp
  admin_commands.cpp
  ttl_commands.cpp
  script_commands.cpp
  transaction_commands.cpp
  pubsub_commands.cpp
  stream_commands.cpp
  replication_commands.cpp
  acl_commands.cpp
  cluster_commands.cpp
  bitmap_commands.cpp
  hyperloglog_commands.cpp
  geospatial_commands.cpp
  client_commands.cpp
  blocking_manager.cpp
)

target_link_libraries(astra_commands PUBLIC
  astra_container
  astra_zset
  astra_base
  astra_protocol
  asio::asio
  cxxopts::cxxopts
  tomlplusplus::tomlplusplus
  absl::random_random
  leveldb
  prometheus-cpp::core
  prometheus-cpp::pull
  concurrentqueue
)

# Link FlatBuffers
if(TARGET flatbuffers::flatbuffers)
  target_link_libraries(astra_commands PUBLIC flatbuffers::flatbuffers)
endif()

# Link TBB for parallel operations
if(TARGET TBB::tbb)
  target_link_libraries(astra_commands PUBLIC TBB::tbb)
endif()

# Add libgossip include paths for cluster commands
if(TARGET libgossip)
  target_include_directories(astra_commands PRIVATE
    ${libgossip_download_SOURCE_DIR}/include
    ${libgossip_download_SOURCE_DIR}/third_party
    ${libgossip_download_SOURCE_DIR}/third_party/json/single_include
  )
  target_link_libraries(astra_commands PUBLIC libgossip)
endif()

# Link Lua library
if(TARGET lua::lua)
  target_link_libraries(astra_commands PUBLIC lua::lua)
endif()

# Create wrapper target for whole-archive linking
add_library(astra_commands_whole INTERFACE)
target_link_libraries(astra_commands_whole INTERFACE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,astra_commands>"
)