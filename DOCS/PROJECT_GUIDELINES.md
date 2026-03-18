# AstraDB Project Guidelines

This document outlines the coding standards, conventions, and architectural guidelines for the AstraDB project.

## Third-Party Dependencies

### Core Libraries

**Networking & Async**
- **Asio 1.30.2** - Async networking with coroutine support, dual backend (epoll/io_uring)
- **libgossip 1.2.1.3** - Gossip protocol implementation for cluster communication

**Serialization & Storage**
- **FlatBuffers 24.3.25** - Zero-copy serialization for AOF/RDB persistence
- **LevelDB** - Lightweight key-value storage
- **zstd 1.5.6** - Fast compression algorithm for data compression

**Memory & Containers**
- **mimalloc 2.1.7** - High-performance memory allocator (20-30% faster than glibc malloc)
- **Abseil 20240116.1** - Google's C++ common libraries with high-performance containers
- **concurrentqueue 1.0.4** - Lock-free MPMC queue
- **Intel TBB 2021.12.0** - Work-stealing task scheduler

**Logging & Monitoring**
- **spdlog 1.17.0** - High-performance structured logging
- **fmt 10.2.1** - Formatting library (spdlog dependency)
- **Prometheus Client 1.2.4** - Metrics collection and monitoring

**Scripting & Configuration**
- **Lua 5.4.7** - Server-side scripting support
- **tomlplusplus 3.4.0** - TOML configuration parser
- **nlohmann_json 3.11.2** - JSON parsing

**Utilities**
- **cxxopts 3.2.1** - Command-line argument parsing
- **sha1** - SHA1 hashing (for Lua script caching)

**Testing & Benchmarking**
- **GoogleTest 1.14.0** - Unit testing framework
- **Google Benchmark 1.8.5** - Performance benchmarking

### Dependency Management
- Use CPM (C++ Package Manager) for all third-party dependencies
- Prefer header-only libraries to reduce compilation time
- Configure platform-specific settings (e.g., macOS ARM64 atomic operations)
- Support both static linking and LTO optimization

## Commit Guidelines

### Commit Message Format
Follow conventional commit format:
- `feat: add new feature`
- `fix: fix bug`
- `docs: update documentation`
- `test: add tests`
- `refactor: refactor code`
- `perf: performance improvement`
- `ci: CI/CD changes`

### Branch Strategy
- **main** - Stable release branch
- **develop** - Development branch
- Use Pull Requests for merging to main

### Common Commit Types

**Bug Fixes**
- Fix logging level issues (e.g., EOF connection errors)
- Fix cross-platform compilation issues
- Fix library linking problems

**Feature Development**
- Add development configuration support
- Enhance user experience (startup banners, colorful output)
- Version management system
- CMake presets for different backends

**Documentation**
- Update design documents
- Update README and implementation status
- Keep documentation in sync with code

**CI/CD**
- Add build variants (epoll/io_uring backends)
- Optimize release workflows

## Coding Standards

### Naming Conventions

**Files**
- Use lowercase with underscores: `string_commands.cpp`

**Classes**
- Use PascalCase: `CommandHandler`

**Functions**
- Use PascalCase: `HandleGet`, `ProcessCommand`

**Variables**
- Use snake_case: `expire_time_ms`, `connection_id`

**Constants/Macros**
- Use UPPER_SNAKE_CASE: `ASTRADB_LOG_INFO`, `MAX_CONNECTIONS`

### Code Organization

```cpp
// ==============================================================================
// Module Description
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <system_headers>
#include <third_party_headers>

#include "project_headers"

namespace astra::module {

// Function implementations

}  // namespace astra::module
```

### Formatting

Based on Google C++ Style Guide:
- **Indentation**: 2 spaces
- **Line width**: 80 characters
- **Brace style**: Attach (`if (x) {`)
- **Pointer alignment**: Left (`Type* ptr`)
- **Include ordering**: System libraries, third-party, project headers

Use `.clang-format` configuration for automatic formatting.

### Error Handling

```cpp
// Return CommandResult for command implementations
CommandResult HandleCommand(const Command& cmd, CommandContext* ctx) {
  if (invalid) {
    return CommandResult(false, "ERR descriptive error message");
  }
  return CommandResult(RespValue(result));
}

// Use std::optional for optional values
std::optional<Value> GetValue(const std::string& key);
```

### Logging

```cpp
// Use logging macros
ASTRADB_LOG_TRACE("Detailed trace information");
ASTRADB_LOG_DEBUG("Debug information");
ASTRADB_LOG_INFO("General information");
ASTRADB_LOG_WARN("Warning conditions");
ASTRADB_LOG_ERROR("Error conditions");
ASTRADB_LOG_CRITICAL("Critical failures");
```

### Comment Style

```cpp
// File header: purpose and license
// ==============================================================================
// String Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

// Function comments for Redis commands
// GET key
CommandResult HandleGet(const Command& cmd, CommandContext* ctx);

// Inline comments for complex logic
// Parse options: NX (set only if not exists), XX (set only if exists)
for (size_t i = 2; i < cmd.ArgCount(); ++i) {
  // Parse each option
}
```

### Type Usage

```cpp
// Prefer std::string for storage
std::string key = command[0].AsString();

// Use absl::string_view for zero-copy passing
void ProcessKey(absl::string_view key);

// Use auto for type deduction where clear
auto result = db->Get(key);

// Use explicit types for public APIs
std::optional<int64_t> GetExpireTime(const std::string& key);
```

### Modern C++ Features

- Use C++23 standard
- `std::optional` for optional values
- `std::chrono` for time handling
- Lambda expressions for callbacks
- RAII for resource management
- `constexpr` for compile-time constants

## Architecture Principles

### Core Design Philosophy

**NO SHARING Architecture**
- Each Worker is completely independent
- Private resources per worker
- No shared state between workers

**MPSC Communication**
- Multi-Producer Single-Consumer queues
- Lock-free cross-worker communication
- Zero-copy message passing

**Multi-Backend Support**
- epoll backend (stable, compatible)
- io_uring backend (high performance, Linux 5.1+)
- Configurable at build time

**Redis Compatibility**
- 250+ Redis commands implemented
- 96%+ Redis 7.4.1 compatibility
- RESP2 and RESP3 protocol support

### Technical Highlights

1. **SIMD Optimization**
   - AVX2 support for x86_64
   - SSE4.2 for vectorized operations
   - NEON for ARM64 architecture

2. **Zero-Copy Serialization**
   - FlatBuffers for efficient serialization
   - No data copying during persistence

3. **High-Performance Memory**
   - mimalloc allocator
   - String pooling for frequent keys
   - Custom object pools

4. **Async Processing**
   - Asio coroutines
   - Non-blocking I/O
   - Efficient event handling

5. **Cluster Support**
   - Gossip protocol for node discovery
   - Automatic shard management
   - Failure detection

6. **Persistence**
   - AOF (Append Only File)
   - RDB snapshots
   - LevelDB for cold storage

### Performance Targets

| Metric | Current | Target |
|--------|---------|--------|
| GET QPS | 62 Kops/s | 1M ops/s |
| SET QPS | 63 Kops/s | 800K ops/s |
| Command Coverage | 250+ (96%+) | 100% |
| Latency vs Redis | -41% | -50% |

### Module Structure

```
src/astra/
├── base/           # Core utilities, logging, config
├── commands/       # Redis command implementations
├── container/      # Data structures (DashMap, ZSet, List)
├── core/           # Core functionality (memory, metrics)
├── network/        # Networking layer (RESP protocol)
├── server/         # Server core (Server, Shard, Worker)
├── persistence/    # Persistence layer (AOF, RDB, LevelDB)
├── cluster/        # Cluster management (Gossip)
├── security/       # Security layer (ACL)
├── replication/    # Replication manager
└── storage/        # Storage utilities
```

## Development Workflow

### Setting Up Development Environment

1. Install dependencies (CMake 3.20+, C++23 compiler)
2. Configure build: `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`
3. Build: `ninja -C build`
4. Run tests: `./build/bin/astradb_tests`
5. Run benchmarks: `./build/bin/astradb_benchmarks`

### Code Review Process

1. Create feature branch from `develop`
2. Make changes with clear commit messages
3. Run tests and benchmarks
4. Format code with `clang-format`
5. Run static analysis with `clang-tidy`
6. Create Pull Request to `develop`
7. Address review feedback
8. Merge after approval

### Testing

- Unit tests for all new features
- Integration tests for command compatibility
- Benchmark tests for performance regression
- Redis-cli compatibility tests

### Documentation

- Update design documents for architectural changes
- Update README for user-facing changes
- Add inline comments for complex logic
- Keep implementation status in sync

## Cross-Platform Considerations

### Windows
- Use Windows-compatible socket APIs
- Handle path separators correctly
- Avoid Unix-specific signals (SIGPIPE)

### macOS
- Disable SSE instructions on ARM64
- Handle atomic operations correctly
- Use platform-specific optimizations

### Linux
- Support both epoll and io_uring backends
- Enable NUMA-aware scheduling
- Use Linux-specific optimizations where beneficial

## Performance Optimization Guidelines

1. **Prefer zero-copy operations** (string_view, Span)
2. **Use lock-free data structures** (concurrentqueue)
3. **Minimize memory allocations** (object pools, string pooling)
4. **Leverage SIMD** for vectorizable operations
5. **Profile before optimizing** (use Google Benchmark)
6. **Avoid premature optimization**

## Security Considerations

1. **Input Validation**: Validate all user inputs
2. **ACL Enforcement**: Respect access control lists
3. **Resource Limits**: Enforce connection and memory limits
4. **Authentication**: Support password-based authentication
5. **Secure Defaults**: Default to secure configurations

## License

All code is licensed under Apache 2.0. Ensure proper attribution for third-party libraries and maintain license headers in all files.

---

**Last Updated**: March 18, 2026
**Maintainer**: caomengxuan666