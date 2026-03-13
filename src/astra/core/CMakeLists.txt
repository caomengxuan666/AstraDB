# ==============================================================================
# astra_core - Core abstractions (async, memory, metrics)
# ==============================================================================

add_library(astra_core STATIC)

target_include_directories(astra_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# FlatBuffers headers are now generated in prebuild.cmake (MUST COMPILE FIRST!)
if(flatbuffers_ADDED OR TARGET flatbuffers::flatbuffers OR TARGET flatbuffers::flatc)
  # CRITICAL: Force flatbuffers_generated to compile BEFORE ANYTHING ELSE
  if(TARGET flatbuffers_generated)
    add_dependencies(astra_core flatbuffers_generated)
    target_include_directories(astra_core PUBLIC
      ${CMAKE_CURRENT_BINARY_DIR}/generated
      ${CMAKE_CURRENT_SOURCE_DIR}/../../../.cpm-cache/flatbuffers/2c40/include
    )
  endif()
endif()

target_sources(astra_core PRIVATE
  async/thread_pool.cpp
  async/new_io_context_pool.cpp
  metrics.cpp
  metrics_http.cpp
)

# Link FlatBuffers
if(TARGET flatbuffers::flatbuffers)
  target_link_libraries(astra_core PUBLIC flatbuffers::flatbuffers)
endif()

target_link_libraries(astra_core PUBLIC
  astra_base
)

# Link Abseil
if(TARGET absl::strings)
  target_link_libraries(astra_core PUBLIC absl::strings)
endif()
if(TARGET absl::flat_hash_map)
  target_link_libraries(astra_core PUBLIC absl::flat_hash_map)
endif()
if(TARGET absl::btree)
  target_link_libraries(astra_core PUBLIC absl::btree)
endif()

# Link Prometheus (core + pull for Exposer)
if(TARGET prometheus-cpp::core)
  target_link_libraries(astra_core PUBLIC prometheus-cpp::core)
endif()
if(TARGET prometheus-cpp::pull)
  target_link_libraries(astra_core PUBLIC prometheus-cpp::pull)
endif()

# Link asio
if(TARGET asio::asio)
  target_link_libraries(astra_core PUBLIC asio::asio)
elseif(TARGET asio)
  target_link_libraries(astra_core PUBLIC asio)
endif()

# Link date

# Link concurrentqueue
if(TARGET concurrentqueue)
  target_link_libraries(astra_core PUBLIC concurrentqueue)
endif()

# Link TBB
if(TARGET TBB::tbb)
  target_link_libraries(astra_core PUBLIC TBB::tbb)
endif()

target_compile_features(astra_core PUBLIC cxx_std_23)