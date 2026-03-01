if(ASTRADB_BUILD_TESTS)
  target_sources(astradb_tests PRIVATE
    resp_test.cpp
  )
  
  target_link_libraries(astradb_tests PRIVATE
    astra_protocol_resp
  )
endif()