# ==============================================================================
# astra_lua - Lua scripting support
# ==============================================================================

add_library(astra_lua INTERFACE)

target_include_directories(astra_lua INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(astra_lua INTERFACE
  astra_engine
)

# Link lua if available
if(TARGET lua)
  target_link_libraries(astra_lua INTERFACE lua)
elseif(TARGET lua::lua)
  target_link_libraries(astra_lua INTERFACE lua::lua)
endif()