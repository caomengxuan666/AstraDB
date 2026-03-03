# ==============================================================================
# astra_base - Basic utilities and infrastructure
# ==============================================================================

add_library(astra_base STATIC)

target_include_directories(astra_base PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_sources(astra_base PRIVATE
  logging.cpp
  config.cpp
)

# Link libraries with conditional checks
if(TARGET mimalloc-static)
  target_link_libraries(astra_base PUBLIC mimalloc-static)
endif()

if(TARGET spdlog::spdlog)
  target_link_libraries(astra_base PUBLIC spdlog::spdlog)
elseif(TARGET spdlog)
  target_link_libraries(astra_base PUBLIC spdlog)
endif()

if(TARGET tomlplusplus::tomlplusplus)
  target_link_libraries(astra_base PUBLIC tomlplusplus::tomlplusplus)
endif()

# Compile definitions
target_compile_definitions(astra_base PUBLIC
  $<$<CONFIG:Release>:NDEBUG>
  $<$<CONFIG:Debug>:ASTRADB_DEBUG>
)