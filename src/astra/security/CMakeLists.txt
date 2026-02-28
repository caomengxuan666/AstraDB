# ==============================================================================
# astra_security - Security module
# ==============================================================================

add_library(astra_security INTERFACE)

target_include_directories(astra_security INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(astra_security INTERFACE
  astra_server
)

if(ASTRADB_ENABLE_TLS)
  target_link_libraries(astra_security INTERFACE OpenSSL::SSL OpenSSL::Crypto)
endif()