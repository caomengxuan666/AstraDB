# ==============================================================================
# astra_persistence - Persistence layer (header-only)
# ==============================================================================

add_library(astra_persistence INTERFACE)

target_include_directories(astra_persistence INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
  ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# FlatBuffers headers are now generated in prebuild.cmake (MUST COMPILE FIRST!)
if(flatbuffers_ADDED OR TARGET flatbuffers::flatbuffers OR TARGET flatbuffers::flatc)
  # CRITICAL: Force flatbuffers_generated to compile BEFORE ANYTHING ELSE
  if(TARGET flatbuffers_generated)
    add_dependencies(astra_persistence flatbuffers_generated)
    target_include_directories(astra_persistence INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../../../.cpm-cache/flatbuffers/2c40/include)
  endif()
endif()

# Link dependencies
target_link_libraries(astra_persistence INTERFACE
 astra_container
  astra_base
  zstd::zstd
  absl::strings
  absl::time
  absl::flat_hash_map
  spdlog::spdlog
)

# Add zstd include directory (propagates to all dependent targets)
if(zstd_SOURCE_DIR)
  target_include_directories(astra_persistence INTERFACE ${zstd_SOURCE_DIR}/lib)
endif()

# Platform-specific settings
if(WIN32)
  target_compile_definitions(astra_persistence INTERFACE
    _WIN32_WINNT=0x0601
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
endif()
