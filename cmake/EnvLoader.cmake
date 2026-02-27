# ==============================================================================
# Environment Variable Loader
# ==============================================================================
# This module provides functionality to load environment variables from .env file
# ==============================================================================

function(read_env_file)
  set(env_file "${CMAKE_CURRENT_SOURCE_DIR}/.env")

  if(EXISTS ${env_file})
    message(STATUS "Loading .env file: ${env_file}")

    file(STRINGS ${env_file} env_lines REGEX "^[^#].*=.*")

    foreach(line ${env_lines})
      # Skip empty lines
      if(NOT line MATCHES "^\\s*$")
        # Extract variable name and value
        string(REGEX MATCH "^([^=]+)=(.*)$" _match ${line})
        if(CMAKE_MATCH_1 AND CMAKE_MATCH_2)
          set(var_name ${CMAKE_MATCH_1})
          set(var_value ${CMAKE_MATCH_2})

          # Strip whitespace
          string(STRIP ${var_name} var_name)
          string(STRIP ${var_value} var_value)

          # Remove quotes if present
          string(REPLACE "\"" "" var_value ${var_value})

          # Set the variable
          set(${var_name}
              ${var_value}
              PARENT_SCOPE)

          # Also set it as environment variable for child processes
          set(ENV{${var_name}} ${var_value})

          message(STATUS "  ${var_name}=${var_value}")
        endif()
      endif()
    endforeach()
  else()
    message(STATUS ".env file not found, using system defaults")
  endif()
endfunction()