# ==============================================================================
# Post-Dependency Injection
# This file is called after all CPM packages are added
# ==============================================================================

# Inject nlohmann_json into libgossip's third_party directory
if(libgossip_ADDED AND nlohmann_json_ADDED)
  set(LIBGOSSIP_JSON_DIR "${libgossip_SOURCE_DIR}/third_party/json/single_include/nlohmann")
  file(MAKE_DIRECTORY "${LIBGOSSIP_JSON_DIR}")

  # Copy nlohmann header files to libgossip's third_party directory
  file(COPY "${nlohmann_json_SOURCE_DIR}/single_include/nlohmann/"
       DESTINATION "${LIBGOSSIP_JSON_DIR}/")

  message(STATUS "✅ nlohmann_json injected to libgossip's third_party: ${LIBGOSSIP_JSON_DIR}")
endif()

# Inject ASIO into libgossip's third_party directory
if(libgossip_ADDED AND asio_ADDED)
  set(LIBGOSSIP_ASIO_TARGET "${libgossip_SOURCE_DIR}/third_party/asio/asio/include")
  file(MAKE_DIRECTORY "${LIBGOSSIP_ASIO_TARGET}")

  file(COPY "${asio_SOURCE_DIR}/asio/include/asio"
      DESTINATION "${LIBGOSSIP_ASIO_TARGET}/")
  file(COPY "${asio_SOURCE_DIR}/asio/include/asio.hpp"
      DESTINATION "${LIBGOSSIP_ASIO_TARGET}/")

  message(STATUS "✅ ASIO injected to libgossip's third_party: ${LIBGOSSIP_ASIO_TARGET}")
endif()

# ==============================================================================
# Exclude unnecessary third-party targets from build
# ==============================================================================

# Exclude Prometheus sample servers and tests
if(TARGET sample_server)
  set_target_properties(sample_server PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(sample_server PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)
endif()
if(TARGET sample_server_multi)
  set_target_properties(sample_server_multi PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(sample_server_multi PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)
endif()
if(TARGET sample_server_auth)
  set_target_properties(sample_server_auth PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(sample_server_auth PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)
endif()
if(TARGET prometheus_core_test)
  set_target_properties(prometheus_core_test PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(prometheus_core_test PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)
endif()
if(TARGET prometheus_pull_test)
  set_target_properties(prometheus_pull_test PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(prometheus_pull_test PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)
endif()
if(TARGET prometheus_util_test)
  set_target_properties(prometheus_util_test PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(prometheus_util_test PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)
endif()

# Exclude benchmark_main (we only need the library)
if(TARGET benchmark_main)
  set_target_properties(benchmark_main PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(benchmark_main PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)
  message(STATUS "✅ Excluded benchmark_main from default build")
endif()

# Exclude gmock_main (we don't use Google Mock)
if(TARGET gmock_main)
  set_target_properties(gmock_main PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(gmock_main PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)
  message(STATUS "✅ Excluded gmock_main from default build")
endif()

# Exclude civetweb (prometheus-cpp dependency, not needed)
if(TARGET civetweb)
  set_target_properties(civetweb PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(civetweb PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)
  message(STATUS "✅ Excluded civetweb from default build")
endif()

message(STATUS "✅ All unnecessary third-party targets excluded from default build")