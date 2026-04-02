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
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
  )
  
  # Add zstd include directory
  if(zstd_SOURCE_DIR)
   target_include_directories(astradb_tests PRIVATE ${zstd_SOURCE_DIR}/lib)
  endif()
  
  target_link_libraries(astradb_tests PRIVATE
   GTest::gtest
   GTest::gtest_main
   GTest::gmock
   mimalloc-static
   spdlog::spdlog
   absl::base
   absl::hash
   absl::flat_hash_map
   absl::synchronization
   astra_base
   astra_persistence
  astra_cluster
  libgossip
  zstd_static
  )
  
  # Directly link libgossip to get PUBLIC include directories
  if(TARGET libgossip)
    target_link_libraries(astradb_tests PRIVATE libgossip)
    # Add libgossip's third_party includes (magic_enum, nlohmann_json, etc.)
    # Note: libgossip uses #include "magic_enum/magic_enum.hpp", so we need the parent dir
    if(DEFINED libgossip_download_SOURCE_DIR)
      target_include_directories(astradb_tests PRIVATE
        ${libgossip_download_SOURCE_DIR}/third_party
        ${libgossip_download_SOURCE_DIR}/third_party/json/single_include
      )
    elseif(DEFINED LIBGOSSIP_SOURCE)
      target_include_directories(astradb_tests PRIVATE
        ${LIBGOSSIP_SOURCE}/third_party
        ${LIBGOSSIP_SOURCE}/third_party/json/single_include
      )
    endif()
  endif()
  
  # Add test subdirectories
  add_subdirectory(core)
  add_subdirectory(memory)
  add_subdirectory(container)
  add_subdirectory(protocol)
  add_subdirectory(commands)
  add_subdirectory(persistence)
  add_subdirectory(cluster)  
  # Register tests
  add_test(NAME unit_tests COMMAND astradb_tests)
endif()