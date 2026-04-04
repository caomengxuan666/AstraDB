add_executable(simple_metadata_test simple_metadata_test.cpp)
target_link_libraries(simple_metadata_test PRIVATE libgossip::core libgossip::network)

# Add libgossip's third_party directory for magic_enum headers
if(TARGET libgossip OR libgossip_SOURCE_DIR)
  target_include_directories(simple_metadata_test PRIVATE
    ${libgossip_SOURCE_DIR}/third_party
  )
endif()

add_executable(test_libgossip_logging test_libgossip_logging.cpp)
target_link_libraries(test_libgossip_logging PRIVATE libgossip::core libgossip::network)

if(TARGET libgossip OR libgossip_SOURCE_DIR)
  target_include_directories(test_libgossip_logging PRIVATE
    ${libgossip_SOURCE_DIR}/third_party
  )
endif()

add_executable(test_3node_metadata_sync test_3node_metadata_sync.cpp)
target_link_libraries(test_3node_metadata_sync PRIVATE libgossip::core libgossip::network)

if(TARGET libgossip OR libgossip_SOURCE_DIR)
  target_include_directories(test_3node_metadata_sync PRIVATE
    ${libgossip_SOURCE_DIR}/third_party
  )
endif()

add_executable(test_temp_id_metadata test_temp_id_metadata.cpp)
target_link_libraries(test_temp_id_metadata PRIVATE libgossip::core libgossip::network)

if(TARGET libgossip OR libgossip_SOURCE_DIR)
  target_include_directories(test_temp_id_metadata PRIVATE
    ${libgossip_SOURCE_DIR}/third_party
  )
endif()