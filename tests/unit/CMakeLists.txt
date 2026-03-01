# ==============================================================================
# Unit Tests
# ==============================================================================

if(ASTRADB_BUILD_TESTS)
  # Unit test executable
  add_executable(astradb_tests
    main.cpp
  )
  
  target_include_directories(astradb_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
  )
  
  target_link_libraries(astradb_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    mimalloc-static
    spdlog::spdlog
    astra_base
    astra_core
  )
  
  # Add test subdirectories
  add_subdirectory(core)
  add_subdirectory(memory)
  
  # Register tests
  add_test(NAME unit_tests COMMAND astradb_tests)
endif()