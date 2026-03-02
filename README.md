# AstraDB

A high-performance, Redis-compatible database written in modern C++.

## Features

- **Redis Protocol Compatible**: Full support for RESP2/RESP3 protocol
- **High Performance**: Optimized C++23 implementation with Asio coroutines
- **Multi-threaded**: Efficient thread pool and task scheduling
- **Scalable**: Sharded architecture with configurable database and shard counts
- **Rich Commands**: Support for 47+ Redis commands including strings, hashes, sets, and sorted sets

## Build

### Prerequisites

- CMake 3.20+
- C++23 compatible compiler (GCC 13+, Clang 16+)
- Ninja build system

### Build Steps

```bash
# Configure and build
cmake -B build -G "Ninja"
ninja -C build

# Run the server
./build/bin/astradb

# Run tests
./build/bin/astradb_tests

# Run benchmarks
./build/bin/astradb_benchmarks
```

### Configuration

You can configure AstraDB using command-line options or a configuration file:

```bash
# Command-line options
./build/bin/astradb --port 6379 --threads 4 --shards 16

# Using configuration file
./build/bin/astradb --config astradb.toml
```

## Performance

AstraDB demonstrates excellent performance compared to Redis:

### Benchmark Results (redis-benchmark, 100K requests, 50 concurrent clients)

#### SET Operations

| Metric | AstraDB | Redis | Improvement |
|--------|---------|-------|-------------|
| QPS | 62,893 | 42,571 | **+48%** |
| Avg Latency | 0.472ms | 0.796ms | **-41%** |
| P95 Latency | 0.871ms | 1.607ms | **-46%** |
| P99 Latency | 1.727ms | 2.791ms | **-38%** |
| Max Latency | 3.391ms | 14.463ms | **-77%** |

#### GET Operations

| Metric | AstraDB | Redis | Improvement |
|--------|---------|-------|-------------|
| QPS | 62,150 | 46,577 | **+33%** |
| Avg Latency | 0.492ms | 0.638ms | **-23%** |
| P95 Latency | 0.863ms | 1.335ms | **-35%** |
| P99 Latency | 1.895ms | 2.015ms | **-6%** |
| Max Latency | 4.079ms | 8.047ms | **-49%** |

**Environment:**
- CPU: Linux 6.8.0-53-generic
- Compiler: GCC 13.3.0
- C++ Standard: C++23
- Build Type: Release with LTO enabled
- Threads: 16 shards distributed across 2 IO contexts

AstraDB outperforms Redis significantly in both throughput and latency, making it an excellent choice for high-performance use cases.

## Project Structure

```
AstraDB/
├── src/
│   ├── astra/
│   │   ├── base/       # Core utilities and logging
│   │   ├── commands/   # Command implementations
│   │   ├── container/  # Data structures (hash, set, zset)
│   │   ├── core/       # Async I/O and thread pool
│   │   ├── network/    # Networking layer
│   │   ├── protocol/   # RESP protocol parser
│   │   └── server/     # Server main logic
│   └── main.cpp
├── tests/
│   ├── benchmark/      # Performance benchmarks
│   └── unit/          # Unit tests
└── cmake/             # CMake configuration
```

## License

Licensed under the Apache License, Version 2.0. See the [LICENSE](LICENSE) file for details.