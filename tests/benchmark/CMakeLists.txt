# ==============================================================================
# Benchmark Tests
# ==============================================================================

if(ASTRADB_BUILD_BENCHMARKS AND TARGET benchmark::benchmark)
  add_executable(astradb_benchmarks
    main.cpp
    executor_bench.cpp
    memory_bench.cpp
  )
  
  target_include_directories(astradb_benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
  )
  
  target_link_libraries(astradb_benchmarks PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main
    mimalloc-static
    spdlog::spdlog
  )
  
  # Register benchmark
  add_test(NAME benchmarks COMMAND astradb_benchmarks --benchmark_min_time=0.1)
endif()