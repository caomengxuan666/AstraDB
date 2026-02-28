# ==============================================================================
# astra_base - Basic utilities and infrastructure
# ==============================================================================

add_library(astra_base INTERFACE)

target_include_directories(astra_base INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Link libraries with conditional checks
if(TARGET mimalloc-static)
  target_link_libraries(astra_base INTERFACE mimalloc-static)
endif()

if(TARGET xxHash::xxhash)
  target_link_libraries(astra_base INTERFACE xxHash::xxhash)
elseif(TARGET xxhash)
  target_link_libraries(astra_base INTERFACE xxhash)
endif()

if(TARGET spdlog::spdlog)
  target_link_libraries(astra_base INTERFACE spdlog::spdlog)
elseif(TARGET spdlog)
  target_link_libraries(astra_base INTERFACE spdlog)
endif()

# Compile definitions
target_compile_definitions(astra_base INTERFACE
  $<$<CONFIG:Release>:NDEBUG>
  $<$<CONFIG:Debug>:ASTRADB_DEBUG>
)