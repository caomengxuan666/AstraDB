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

# ==============================================================================
# Disable Tests and Examples for ALL Third-Party Libraries
# ==============================================================================
# This section disables test and example builds for all third-party dependencies
# to significantly speed up build time and reduce unnecessary compilation

# Abseil (C++ common libraries)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ABSL_BUILD_TEST_HELPERS OFF CACHE BOOL "" FORCE)
set(ABSL_USE_GOOGLETEST_HEAD OFF CACHE BOOL "" FORCE)
set(ABSL_USE_EXTERNAL_GOOGLETEST OFF CACHE BOOL "" FORCE)

# Benchmark (Google Benchmark)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(BENCHMARK_FORCE_WERROR OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_EXCEPTIONS ON CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_LTO OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_LIBPFM OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_ASSEMBLY_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_USE_LIBCXX OFF CACHE BOOL "" FORCE)
set(BENCHMARK_BUILD_32_BITS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_DOXYGEN OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)

# cxxopts (command line parser)
set(CXXOPTS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CXXOPTS_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# date (date/time library)
set(DATE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DATE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# fmt (formatting library)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)

# googletest (Google Test)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)

# leveldb (key-value store)
set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LEVELDB_INSTALL OFF CACHE BOOL "" FORCE)
set(LEVELDB_SHARED OFF CACHE BOOL "" FORCE)

# libgossip (gossip protocol)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_PYTHON_BINDINGS OFF CACHE BOOL "" FORCE)
set(LIBGOSSIP_SKIP_THIRD_PARTY_CHECK ON CACHE BOOL "" FORCE)

# mimalloc (memory allocator)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)

# nlohmann_json (JSON library)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)

# prometheus-cpp (metrics)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PULL ON CACHE BOOL "" FORCE)
set(ENABLE_PUSH OFF CACHE BOOL "" FORCE)
set(ENABLE_COMPRESSION OFF CACHE BOOL "" FORCE)
set(USE_THIRDPARTY_SUBMODULES OFF CACHE BOOL "" FORCE)
set(GENERATE_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(OVERRIDE_FETCH_STRATEGY OFF CACHE BOOL "" FORCE)

# spdlog (logging)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)

# tomlplusplus (TOML parser)
set(TOMLPLUSPLUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# TBB (Threading Building Blocks)
set(TBB_TEST OFF CACHE BOOL "" FORCE)
set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TBB_STRICT OFF CACHE BOOL "" FORCE)
set(TBB_FUZZ_TESTING OFF CACHE BOOL "" FORCE)

# zstd (compression)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)

# sha1 (SHA1 hash)
set(SHA1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SHA1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Lua (scripting)
set(BUILD_LUA OFF CACHE BOOL "" FORCE)  # Don't build Lua standalone executable

# FlatBuffers (serialization)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_GRPCTEST OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_SHAREDLIB OFF CACHE BOOL "" FORCE)

message(STATUS "✅ All third-party tests and examples disabled for faster builds")