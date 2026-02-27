# ==============================================================================
# CPM Configuration and Proxy Settings
# ==============================================================================
# This module configures CPM package manager and proxy settings
# ==============================================================================

# ==============================================================================
# CPM Basic Configuration
# ==============================================================================

# Enable shallow clone by default (faster)
set(CPM_DOWNLOAD_GIT_SHALLOW
    TRUE
    CACHE BOOL "Use shallow git clones for faster downloads")

# Set CPM source cache for faster rebuilds
if(NOT DEFINED CPM_SOURCE_CACHE)
  set(CPM_SOURCE_CACHE
      "${CMAKE_CURRENT_SOURCE_DIR}/.cpm-cache"
      CACHE PATH "CPM source cache directory")
endif()

message(STATUS "CPM source cache: ${CPM_SOURCE_CACHE}")

# ==============================================================================
# CPM Proxy Configuration
# ==============================================================================

# Set up proxy for CPM to speed up downloads in China
# CPM respects HTTP_PROXY and HTTPS_PROXY environment variables
if(DEFINED ENV{HTTP_PROXY})
  message(STATUS "Using HTTP proxy: $ENV{HTTP_PROXY}")
  set(CPM_DOWNLOAD_HTTP_PROXY "$ENV{HTTP_PROXY}")
endif()

if(DEFINED ENV{HTTPS_PROXY})
  message(STATUS "Using HTTPS proxy: $ENV{HTTPS_PROXY}")
  set(CPM_DOWNLOAD_HTTPS_PROXY "$ENV{HTTPS_PROXY}")
endif()

# ==============================================================================
# Git Proxy Configuration
# ==============================================================================

# CPM uses Git to clone repositories, so we need to configure Git proxy too
# For SOCKS5 proxy (like your 7897 port), Git supports it natively

function(configure_git_proxy proxy_env)
  if(NOT proxy_env)
    return()
  endif()

  # Extract protocol, host and port from proxy URL
  # Supports formats: http://127.0.0.1:7897, socks5://127.0.0.1:7897, socks5h://127.0.0.1:7897
  if(proxy_env MATCHES "^([a-z]+)://([^:]+):([0-9]+)$")
    set(PROXY_PROTOCOL ${CMAKE_MATCH_1})
    set(PROXY_HOST ${CMAKE_MATCH_2})
    set(PROXY_PORT ${CMAKE_MATCH_3})

    # Determine Git proxy protocol
    # Git supports: http, https, socks5, socks5h
    string(TOLOWER ${PROXY_PROTOCOL} PROXY_PROTOCOL_LOWER)

    if(PROXY_PROTOCOL_LOWER STREQUAL "socks5" OR PROXY_PROTOCOL_LOWER STREQUAL
                                                 "socks5h")
      set(GIT_PROXY "${PROXY_PROTOCOL_LOWER}://${PROXY_HOST}:${PROXY_PORT}")
    else()
      # For HTTP proxies, Git also works with http:// protocol
      set(GIT_PROXY "http://${PROXY_HOST}:${PROXY_PORT}")
    endif()

    # Configure Git to use the proxy
    execute_process(COMMAND git config --global http.proxy "${GIT_PROXY}"
                    OUTPUT_QUIET ERROR_QUIET)

    execute_process(COMMAND git config --global https.proxy "${GIT_PROXY}"
                    OUTPUT_QUIET ERROR_QUIET)

    message(STATUS "Configured Git proxy: ${GIT_PROXY}")
  else()
    message(WARNING "Invalid proxy format: ${proxy_env}")
  endif()
endfunction()

# Configure Git proxy based on HTTP_PROXY or HTTPS_PROXY
if(DEFINED ENV{HTTP_PROXY})
  configure_git_proxy("$ENV{HTTP_PROXY}")
elseif(DEFINED ENV{HTTPS_PROXY})
  configure_git_proxy("$ENV{HTTPS_PROXY}")
endif()

# ==============================================================================
# GitHub Mirror Configuration (Speed Up Downloads)
# ==============================================================================

# Configure Git to use GitHub mirror for faster downloads in China
# Uncomment one of the mirrors below to use it:

# Option 1: ghproxy (fastest)
# Note: Disabled due to URL parsing issues. Using proxy only.
# execute_process( COMMAND git config --global
# url."https://ghproxy.com/github.com/".insteadOf "https://github.com/"
# OUTPUT_QUIET ERROR_QUIET )
# message(STATUS "Using GitHub mirror: ghproxy.com")

message(STATUS "Using proxy only (GitHub mirror disabled)")

# Option 2: mirror.ghproxy (alternative)
# execute_process( COMMAND git config --global
# url."https://mirror.ghproxy.com/https://github.com/".insteadOf
# "https://github.com/" OUTPUT_QUIET ERROR_QUIET )
# message(STATUS "Using GitHub mirror: mirror.ghproxy.com")

# Option 3: Gitee (GitHub mirror on Gitee)
# Note: You need to manually sync the repository to Gitee first
# execute_process( COMMAND git config --global
# url."https://gitee.com/mirrors/".insteadOf "https://github.com/"
# OUTPUT_QUIET ERROR_QUIET )
# message(STATUS "Using GitHub mirror: gitee.com")

# Option 4: gitclone (another fast mirror)
# execute_process( COMMAND git config --global
# url."https://gitclone.com/github.com/".insteadOf
# "https://github.com/" OUTPUT_QUIET ERROR_QUIET )
# message(STATUS "Using GitHub mirror: gitclone.com")

# ==============================================================================
# Pre-dependency Configuration
# ==============================================================================

# Disable -Werror for third-party libraries BEFORE adding packages
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(GTEST_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(FMT_COMPILE FALSE CACHE BOOL "" FORCE)