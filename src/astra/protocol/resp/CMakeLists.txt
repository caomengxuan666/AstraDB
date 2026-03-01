# ==============================================================================
# RESP Protocol - RESP2/RESP3 Parser and Builder
# ==============================================================================

add_library(astra_protocol_resp
  resp_parser.cpp
  resp_builder.cpp
)

target_include_directories(astra_protocol_resp PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(astra_protocol_resp PUBLIC
  absl::base
  absl::strings
)

target_compile_features(astra_protocol_resp PUBLIC cxx_std_23)