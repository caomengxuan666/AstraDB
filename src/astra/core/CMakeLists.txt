# ==============================================================================
# astra_core - Core abstractions (async, memory, metrics)
# ==============================================================================

add_library(astra_core INTERFACE)

target_include_directories(astra_core INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(astra_core INTERFACE
  astra_base
)

# Link Abseil
if(TARGET absl::strings)
  target_link_libraries(astra_core INTERFACE absl::strings)
endif()
if(TARGET absl::flat_hash_map)
  target_link_libraries(astra_core INTERFACE absl::flat_hash_map)
endif()
if(TARGET absl::btree)
  target_link_libraries(astra_core INTERFACE absl::btree)
endif()

# Link Prometheus
if(TARGET prometheus-cpp::core)
  target_link_libraries(astra_core INTERFACE prometheus-cpp::core)
endif()

# Link asio
if(TARGET asio::asio)
  target_link_libraries(astra_core INTERFACE asio::asio)
elseif(TARGET asio)
  target_link_libraries(astra_core INTERFACE asio)
endif()

# Link date
if(TARGET date::date)
  target_link_libraries(astra_core INTERFACE date::date)
endif()

target_compile_features(astra_core INTERFACE cxx_std_23)