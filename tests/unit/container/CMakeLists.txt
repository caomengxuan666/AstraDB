if(ASTRADB_BUILD_TESTS)
  target_sources(astradb_tests PRIVATE
    dash_map_test.cpp
    zset_test.cpp
  )
  
  # Link zset implementation
  target_sources(astradb_tests PRIVATE
    $<TARGET_OBJECTS:astra_zset>
  )
endif()