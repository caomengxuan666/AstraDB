# ==============================================================================
# astra_core - Core abstractions (async, memory, metrics)
# ==============================================================================

add_library(astra_core STATIC)

target_include_directories(astra_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Include FlatBuffers BuildFlatBuffers module
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../.cpm-cache/flatbuffers/2c40/CMake/BuildFlatBuffers.cmake")
  include(${CMAKE_CURRENT_SOURCE_DIR}/../../.cpm-cache/flatbuffers/2c40/CMake/BuildFlatBuffers.cmake)
endif()

# Generate FlatBuffers headers for metrics schema
if(flatbuffers_ADDED OR TARGET flatbuffers::flatbuffers OR TARGET flatbuffers::flatc)
  set(METRICS_FBS ${CMAKE_CURRENT_SOURCE_DIR}/metrics.fbs)
  set(METRICS_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
  
  build_flatbuffers(
    "${METRICS_FBS}"
    ""  # No include dirs
    "metrics_generated"
    ""  # No additional dependencies
    "${METRICS_GENERATED_DIR}"
    ""  # No binary schemas
    ""  # No text schema copy
  )
  
  target_include_directories(astra_core PUBLIC ${METRICS_GENERATED_DIR})
  add_dependencies(astra_core metrics_generated)
  
  # Add FlatBuffers include directory
  target_include_directories(astra_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../.cpm-cache/flatbuffers/2c40/include)
  
  message(STATUS "FlatBuffers metrics headers will be generated in ${METRICS_GENERATED_DIR}")
endif()

target_sources(astra_core PRIVATE
  async/thread_pool.cpp
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

# Link Prometheus (only core, we use our own ASIO HTTP server)
if(TARGET prometheus-cpp::core)
  target_link_libraries(astra_core PUBLIC prometheus-cpp::core)
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