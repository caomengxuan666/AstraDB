# ==============================================================================
# astra_commands - Command implementations
# ==============================================================================

# Create separate command library for whole-archive linking
add_library(astra_commands STATIC)

target_include_directories(astra_commands PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

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
)

target_link_libraries(astra_commands PUBLIC
  astra_container
  astra_zset
  astra_base
  astra_protocol
  cxxopts::cxxopts
  tomlplusplus::tomlplusplus
)

# Link Lua library
if(TARGET lua::lua)
  target_link_libraries(astra_commands PUBLIC lua::lua)
endif()

# Link OpenSSL for SHA1
if(OPENSSL_FOUND)
  target_link_libraries(astra_commands PUBLIC OpenSSL::Crypto)
endif()

# Create wrapper target for whole-archive linking
add_library(astra_commands_whole INTERFACE)
target_link_libraries(astra_commands_whole INTERFACE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,astra_commands>"
)