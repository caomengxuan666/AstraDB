# ==============================================================================
# FlatBuffers Generated Headers Management
# ==============================================================================
# This file creates an interface library for all generated headers
# This ensures all headers are generated before any compilation starts
# ==============================================================================

# Create an interface library that represents all generated headers
add_library(generated_headers INTERFACE)

# Add include directories for all generated headers
target_include_directories(generated_headers INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}/src/astra/core/generated
    ${CMAKE_CURRENT_BINARY_DIR}/src/astra/commands/generated
    ${CMAKE_CURRENT_BINARY_DIR}/src/astra/persistence/generated
    ${CMAKE_CURRENT_BINARY_DIR}/src/astra/cluster/generated
)

# Make this interface library depend on all header generation targets
# This ensures headers are generated before any source that links to this library
if(TARGET metrics_generated)
    add_dependencies(generated_headers metrics_generated)
endif()

if(TARGET command_cache_generated)
    add_dependencies(generated_headers command_cache_generated)
endif()

if(TARGET rdb_generated)
    add_dependencies(generated_headers rdb_generated)
endif()

if(TARGET aof_generated)
    add_dependencies(generated_headers aof_generated)
endif()

if(TARGET cluster_message_generated)
    add_dependencies(generated_headers cluster_message_generated)
endif()

# Make all project libraries depend on generated_headers
# This ensures headers are always generated before any compilation
if(TARGET astra_base)
    target_link_libraries(astra_base PUBLIC generated_headers)
endif()

if(TARGET astra_commands)
    target_link_libraries(astra_commands PUBLIC generated_headers)
endif()

if(TARGET astra_network)
    target_link_libraries(astra_network PUBLIC generated_headers)
endif()

if(TARGET astra_server)
    target_link_libraries(astra_server PUBLIC generated_headers)
endif()

if(TARGET astra_cluster)
    target_link_libraries(astra_cluster PUBLIC generated_headers)
endif()

if(TARGET astradb)
    target_link_libraries(astradb PRIVATE generated_headers)
endif()

# Force precompiled headers to depend on header generation
if(TARGET astra_base_pch)
    add_dependencies(astra_base_pch generated_headers)
endif()

if(TARGET astra_commands_pch)
    add_dependencies(astra_commands_pch generated_headers)
endif()

if(TARGET astra_server_pch)
    add_dependencies(astra_server_pch generated_headers)
endif()

if(TARGET astradb_pch)
    add_dependencies(astradb_pch generated_headers)
endif()

message(STATUS "✅ Generated headers interface library created and linked to all targets")