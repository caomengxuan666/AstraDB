# ==============================================================================
# astra_core - Core abstractions (async, memory, metrics)
# ==============================================================================

add_library(astra_core STATIC)

target_include_directories(astra_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_sources(astra_core PRIVATE
  async/thread_pool.cpp
)

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

# Link Prometheus
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
if(TARGET date::date)
  target_link_libraries(astra_core PUBLIC date::date)
endif()

# Link concurrentqueue
if(TARGET concurrentqueue)
  target_link_libraries(astra_core PUBLIC concurrentqueue)
endif()

# Link TBB
if(TARGET TBB::tbb)
  target_link_libraries(astra_core PUBLIC TBB::tbb)
endif()

target_compile_features(astra_core PUBLIC cxx_std_23)