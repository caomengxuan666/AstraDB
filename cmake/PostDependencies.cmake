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