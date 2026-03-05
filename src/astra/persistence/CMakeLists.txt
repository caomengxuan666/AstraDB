# ==============================================================================
# astra_persistence - Persistence layer (header-only)
# ==============================================================================

add_library(astra_persistence INTERFACE)

target_include_directories(astra_persistence INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Generate FlatBuffers headers
if(TARGET flatbuffers::flatbuffers)
  find_program(FLATC flatc PATHS ${flatbuffers_SOURCE_DIR} NO_DEFAULT_PATH)
  
  if(FLATC)
    set(RDB_FBS ${CMAKE_CURRENT_SOURCE_DIR}/rdb.fbs)
    set(RDB_GENERATED_HEADER ${CMAKE_CURRENT_BINARY_DIR}/rdb_generated.h)
    
    add_custom_command(
      OUTPUT ${RDB_GENERATED_HEADER}
      COMMAND ${FLATC} --cpp --cpp-std c++17 -o ${CMAKE_CURRENT_BINARY_DIR} ${RDB_FBS}
      DEPENDS ${RDB_FBS}
      COMMENT "Generating FlatBuffers headers for RDB schema"
      VERBATIM
    )
    
    add_custom_target(rdb_flatbuffers_gen DEPENDS ${RDB_GENERATED_HEADER})
    
    target_include_directories(astra_persistence INTERFACE ${CMAKE_CURRENT_BINARY_DIR})
    
    target_sources(astra_persistence INTERFACE ${RDB_GENERATED_HEADER})
    target_link_libraries(astra_persistence INTERFACE flatbuffers::flatbuffers)
  else()
    message(WARNING "flatc not found, FlatBuffers support disabled")
  endif()
endif()

# Link dependencies
target_link_libraries(astra_persistence INTERFACE
  astra_engine
  leveldb
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
