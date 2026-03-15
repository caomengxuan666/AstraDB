## Overview
This PR fixes Windows compilation errors for the NO SHARING architecture and improves cross-platform socket handling using ASIO abstractions.

## Key Changes

### 1. Windows Compilation Fixes
- Add _WIN32_WINNT=0x0601 definition for Windows 7+ compatibility
- Fix sys/socket.h inclusion for cross-platform compatibility
  - Use winsock2.h/windows.h on Windows
  - Use sys/socket.h on Linux/Unix

### 2. Cross-Platform Socket Options (ASIO Abstraction)
- Use ASIO's socket_base::reuse_address(true) as cross-platform default
- On Linux/Unix: Add SO_REUSEPORT for kernel-level load balancing
- On Windows: Use single acceptor mode (SO_REUSEPORT not supported)
- Let ASIO handle platform differences automatically

### 3. SO_REUSEPORT Support
- Enable SO_REUSEPORT for kernel-level connection distribution on Linux
- Add detailed logging for SO_REUSEPORT status in Worker constructor
- Log kernel-level connection distribution message when enabled
- Warn when SO_REUSEPORT is not supported on the platform

### 4. Configuration Updates
- Enable use_per_worker_io in astradb.toml
- Enable use_so_reuseport in astradb.toml

## Technical Details

### Platform Differences
- Linux: Supports SO_REUSEPORT for kernel-level load balancing
- Windows: Does NOT support SO_REUSEPORT (per Microsoft documentation)
  - Windows 10+ has SO_REUSE_UNICASTPORT but not directly available via ASIO
  - Falls back to single acceptor mode using SO_REUSEADDR

### ASIO Benefits
- Simplifies code using ASIO's proven cross-platform abstraction
- ASIO automatically handles platform-specific socket options
- Better maintainability and less platform-specific code
- Reduces risk of platform-specific bugs

## Testing Results
- Compiled successfully on Linux with GCC
- SO_REUSEPORT enabled successfully on Linux (Worker 0 and Worker 1)
- Server runs and responds to basic commands (PING, SET, GET)
- Windows CI should pass (pending CI verification)

## Related Issues
- Fixes Windows compilation errors related to missing _WIN32_WINNT
- Fixes sys/socket.h not found on Windows
- Improves cross-platform compatibility for NO SHARING architecture

## Files Changed
- cmake/PlatformSettings.cmake: Add _WIN32_WINNT definition
- src/astra/core/async/new_io_context_pool.hpp: Use ASIO cross-platform abstractions
- src/astra/core/async/new_io_context_pool.cpp: Implement platform-specific socket options
- src/astra/server/worker.hpp: Add SO_REUSEPORT logging
- astradb.toml: Enable per-worker IO and SO_REUSEPORT

## Commits
- d53ebd3: test: Enable SO_REUSEPORT for kernel-level load balancing
- 27f742a: refactor: Use ASIO's cross-platform abstraction for socket options
- 9b0f27b: fix: Improve Windows SO_REUSEPORT handling with SO_REUSE_UNICASTPORT
- aed3f7d: fix: Resolve Windows compilation errors for NO SHARING architecture