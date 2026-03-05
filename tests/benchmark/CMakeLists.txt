# ==============================================================================
# Benchmark Tests
# ==============================================================================

if(ASTRADB_BUILD_BENCHMARKS AND TARGET benchmark::benchmark)
  add_executable(astradb_benchmarks
    main.cpp
    executor_bench.cpp
    memory_bench.cpp
    zset_bench.cpp
    simd_bench.cpp
    persistence_bench.cpp
    cluster_bench.cpp
    multi_database_bench.cpp
  )
  
  target_include_directories(astradb_benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
  )
  
  target_link_libraries(astradb_benchmarks PRIVATE
    astra_core
    benchmark::benchmark
    mimalloc-static
    spdlog::spdlog
    astra_container
    astra_base
    astra_persistence
    astra_cluster
    astra_commands
    leveldb
    absl::flat_hash_map
    absl::hash
  )
  
  # Link zset implementation
  target_sources(astradb_benchmarks PRIVATE
    $<TARGET_OBJECTS:astra_zset>
  )
  
  # Add B+ Tree include path
  if(TARGET b-plus-tree)
    target_include_directories(astradb_benchmarks PRIVATE
      ${b-plus-tree_SOURCE_DIR}
    )
  endif()
  
  # Add libgossip's third_party includes (magic_enum, nlohmann_json, etc.) for cluster module
  # Note: libgossip uses #include "magic_enum/magic_enum.hpp", so we need the parent dir
  if(DEFINED libgossip_download_SOURCE_DIR)
    target_include_directories(astradb_benchmarks PRIVATE
      ${libgossip_download_SOURCE_DIR}/include
      ${libgossip_download_SOURCE_DIR}/third_party
      ${libgossip_download_SOURCE_DIR}/third_party/json/single_include
    )
  elseif(DEFINED LIBGOSSIP_SOURCE)
    target_include_directories(astradb_benchmarks PRIVATE
      ${LIBGOSSIP_SOURCE}/include
      ${LIBGOSSIP_SOURCE}/third_party
      ${LIBGOSSIP_SOURCE}/third_party/json/single_include
    )
  endif()
  
  # Register benchmark
  add_test(NAME benchmarks COMMAND astradb_benchmarks --benchmark_min_time=0.1)
endif()