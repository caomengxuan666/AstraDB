# ==============================================================================
# astra_commands - Command implementations
# ==============================================================================

# Create separate command library for whole-archive linking
add_library(astra_commands STATIC)

# Note: UNITY BUILD disabled for astra_commands due to: 1. FlatBuffers generated
# header include path issues 2. Command registration uses __LINE__ macro which
# conflicts when files are unified
target_include_directories(
  astra_commands
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../..
         ${CMAKE_CURRENT_SOURCE_DIR}/../cluster
         ${CMAKE_CURRENT_SOURCE_DIR}/../persistence
         ${CMAKE_CURRENT_BINARY_DIR}/generated
         ${CMAKE_CURRENT_BINARY_DIR}/../persistence/generated
         ${CMAKE_CURRENT_BINARY_DIR}/../core/generated
         ${CMAKE_CURRENT_BINARY_DIR}/../cluster/generated
)

# CRITICAL: Force flatbuffers_generated to compile BEFORE ANYTHING ELSE
if(TARGET flatbuffers_generated)
  add_dependencies(astra_commands flatbuffers_generated)
  target_include_directories(astra_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../.cpm-cache/flatbuffers/2c40/include)
endif()

# Add vog/sha1 include directory for SHA1 hashing
if(sha1_SOURCE_DIR)
  target_include_directories(astra_commands PUBLIC ${sha1_SOURCE_DIR})
endif()

# Add zstd include directory (required by aof_writer.hpp in persistence)
if(zstd_SOURCE_DIR)
  target_include_directories(astra_commands PUBLIC ${zstd_SOURCE_DIR}/lib)
endif()

# All command files (including core registration logic)
target_sources(
  astra_commands
  PRIVATE command_handler.cpp
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
          blocking_manager.cpp)

target_link_libraries(
  astra_commands
  PUBLIC astra_container
         astra_zset
         astra_base
         astra_protocol
         asio::asio
         cxxopts::cxxopts
         tomlplusplus::tomlplusplus
         absl::random_random
         prometheus-cpp::core
         concurrentqueue)

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
  target_include_directories(
    astra_commands
    PRIVATE ${libgossip_download_SOURCE_DIR}/include
            ${libgossip_download_SOURCE_DIR}/third_party
            ${libgossip_download_SOURCE_DIR}/third_party/json/single_include)
  target_link_libraries(astra_commands PUBLIC libgossip::core libgossip::network)
endif()

# Link Lua library
if(TARGET lua::lua)
  target_link_libraries(astra_commands PUBLIC lua::lua)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(astra_commands PRIVATE -Wl,--no-fatal-warnings
                        -Wl,--no-warn-search-mismatch)
  endif()
endif()

# Create wrapper target for whole-archive linking
add_library(astra_commands_whole INTERFACE)
target_link_libraries(astra_commands_whole
                      INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,astra_commands>")
# Link astra_network for Connection class
if(TARGET astra_network)
  target_link_libraries(astra_commands PUBLIC astra_network)
endif()
