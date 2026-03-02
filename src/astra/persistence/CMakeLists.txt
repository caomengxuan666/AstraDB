# ==============================================================================
# astra_persistence - Persistence layer
# ==============================================================================

add_library(astra_persistence INTERFACE)

target_include_directories(astra_persistence INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Link dependencies
target_link_libraries(astra_persistence INTERFACE
  astra_engine
  leveldb::leveldb
  absl::strings
  absl::time
  absl::container
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
