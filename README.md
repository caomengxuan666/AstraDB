# AstraDB

A high-performance, Redis-compatible database written in modern C++23.

## 🎯 Vision

**AstraDB is designed to surpass DragonflyDB in every aspect while maintaining full Redis compatibility.**

Our goal: **2x DragonflyDB performance, 50% less memory usage, and superior scalability.**

## ✨ Features

### Core Features ✅

- **Redis Protocol Compatible**: Full support for RESP2 and RESP3 protocols
- **High Performance**: C++23 implementation with Asio coroutines and multi-threading
- **NO SHARING Architecture**: Each Worker is completely independent with private resources
- **MPSC Communication**: Lock-free cross-worker communication via concurrent queues
- **SIMD Optimizations**: AVX2, SSE4.2, and NEON support for vectorized operations
- **Zero-Copy Serialization**: FlatBuffers-based efficient serialization
- **Flexible Threading**: Support for both single-threaded and multi-threaded modes
- **Dual Backend**: epoll (stable) and io_uring (high performance) support
- **Rich Commands**: Support for 250+ Redis commands across all data types
- **Persistence**: AOF (Append Only File), RDB snapshots, and ROCKSDB integration
- **Cluster Support**: Gossip-based cluster management with libgossip
- **Security**: Access Control List (ACL) support
- **Monitoring**: Prometheus metrics integration
- **Logging**: High-performance structured logging with spdlog

### Data Structures ✅

- **String**: Basic key-value operations
- **Hash**: Field-value pairs
- **List**: Linked list with push/pop operations
- **Set**: Unique string collections
- **Sorted Set**: Ordered sets with scores (B+ tree implementation)
- **Stream**: Redis Streams for message queues
- **Bitmap**: Bit manipulation operations
- **HyperLogLog**: Probabilistic cardinality estimation
- **Geospatial**: Geospatial indexing and queries

### Advanced Features ✅

- **Transactions**: MULTI/EXEC with optimistic locking
- **Pub/Sub**: Publish/Subscribe messaging
- **Lua Scripting**: Server-side scripting with Lua 5.4
- **ACL**: User-based access control
- **TTL**: Time-to-live for automatic key expiration
- **Replication**: Master-slave replication (partial)
- **Cluster**: Distributed cluster with gossip protocol

### Planned Features 🚧

- **Real Blocking**: Full blocking command implementation (BLPOP, BRPOP, etc.)
- **Raft Consensus**: Distributed consensus for cluster management
- **TLS Encryption**: Secure connections with OpenSSL
- **Vector Search**: ANN search for Redis Search compatibility
- **Redis Modules compatibility**: Support for Redis Modules API

## 📊 Performance

### Current Performance (Benchmark Results)

**Environment:**
- CPU: Linux 6.8.0-53-generic
- Compiler: GCC 13.3.0
- C++ Standard: C++23
- Build Type: Release with LTO enabled
- Threads: 16 shards distributed across 2 IO contexts

### SET Operations

| Metric | AstraDB | Redis | Improvement |
|--------|---------|-------|-------------|
| QPS | 62,893 | 42,571 | **+48%** |
| Avg Latency | 0.472ms | 0.796ms | **-41%** |
| P95 Latency | 0.871ms | 1.607ms | **-46%** |
| P99 Latency | 1.727ms | 2.791ms | **-38%** |
| Max Latency | 3.391ms | 14.463ms | **-77%** |

### GET Operations

| Metric | AstraDB | Redis | Improvement |
|--------|---------|-------|-------------|
| QPS | 62,150 | 46,577 | **+33%** |
| Avg Latency | 0.492ms | 0.638ms | **-23%** |
| P95 Latency | 0.863ms | 1.335ms | **-35%** |
| P99 Latency | 1.895ms | 2.015ms | **-6%** |
| Max Latency | 4.079ms | 8.047ms | **-49%** |

AstraDB outperforms Redis significantly in both throughput and latency, making it an excellent choice for high-performance use cases.

### Target Performance

| Operation | Redis | DragonflyDB | AstraDB (Target) | Current Status |
|-----------|-------|-------------|------------------|----------------|
| GET | 100 Kops/s | 500 Kops/s | **1M ops/s** | 62 Kops/s (6.2% of target) |
| SET | 80 Kops/s | 400 Kops/s | **800 Kops/s** | 63 Kops/s (7.9% of target) |
| ZADD | 100 Kops/s | 500 Kops/s | **1M ops/s** | TBD |
| ZRANGE | 80 Kops/s | 400 Kops/s | **800 Kops/s** | TBD |

## 🏗️ Architecture

### Current Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        AstraDB Core                            │
├─────────────────────────────────────────────────────────────────┤
│  Server Core (Asio + Thread Pool)                              │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  Server | Shard Manager | Command Handler                 │  │
│  └───────────────────────────────────────────────────────────┘  │
│                              ↓                                   │
│  Network Layer (Asio)                                          │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  Connection Management | RESP2 Parser | Command Registry  │  │
│  └───────────────────────────────────────────────────────────┘  │
│                              ↓                                   │
│  Command Layer (100+ Redis Commands)                          │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  String | Hash | List | Set | ZSet | Stream | Transaction  │  │
│  │  PubSub | Script | Admin | ACL | Bitmap | HyperLogLog       │  │
│  │  Geospatial | Client | Cluster | Replication | TTL           │  │
│  └───────────────────────────────────────────────────────────┘  │
│                              ↓                                   │
│  Data Structures Layer                                         │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  DashMap | B+ Tree ZSet | String Pool | Linked List       │  │
│  │  Stream Data | FlatBuffers Serialization                   │  │
│  └───────────────────────────────────────────────────────────┘  │
│                              ↓                                   │
│  Storage Layer                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  Key Metadata | AOF Writer | RDB Writer | ROCKSDB Adapter  │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘

Cluster & Security:
┌─────────────────────────────────────────────────────────────────┐
│  Gossip Manager (libgossip) | ACL Manager | Replication Manager │
└─────────────────────────────────────────────────────────────────┘
```

### Planned Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Blocking Manager                            │
│  Wait Queue | Timeout Management | Async Notification           │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    Raft Consensus                              │
│  Leader Election | Log Replication | Consensus Protocol         │
└─────────────────────────────────────────────────────────────────┘
```

## 🛠️ Technology Stack

### Core Dependencies

| Component | Library | Version | Purpose |
|-----------|---------|---------|---------|
| **Networking** | Asio | 1.30.2 | Async networking and coroutines |
| **Serialization** | FlatBuffers | 24.3.25 | Zero-copy serialization |
| **Logging** | spdlog | 1.17.0 | High-performance logging |
| **Memory** | mimalloc | 2.1.7 | Fast memory allocator |
| **Container** | Abseil | 20240116.1 | High-performance containers |
| **Thread Pool** | Intel TBB | 2021.12.0 | Work-stealing scheduler |
| **Concurrent Queue** | concurrentqueue | 1.0.4 | Lock-free queue |
| **Cluster** | libgossip | 1.2.0 | Gossip protocol |
| **Storage** | ROCKSDB | Latest | Key-value store |
| **Metrics** | Prometheus Client | 1.2.4 | Metrics collection |
| **Compression** | zstd | 1.5.6 | Fast compression |
| **JSON** | nlohmann_json | 3.11.2 | JSON parsing |
| **Lua** | Lua | 5.4.7 | Scripting support |
| **Config** | tomlplusplus | 3.4.0 | TOML configuration |
| **CLI** | cxxopts | 3.2.1 | Command-line parsing |
| **Testing** | GoogleTest | 1.14.0 | Unit testing |
| **Benchmarking** | Google Benchmark | 1.8.5 | Performance benchmarking |

### Build System

- **CMake**: 3.20+
- **C++ Standard**: C++23
- **Compiler**: GCC 13+ / Clang 16+
- **Build Tool**: Ninja (recommended) or Make
- **Package Manager**: CPM (C++ Package Manager)

## 📦 Project Structure

```
AstraDB/
├── src/
│   ├── astra/
│   │   ├── base/              # Core utilities and logging
│   │   ├── commands/          # Redis command implementations
│   │   ├── container/         # Data structures (DashMap, ZSet, List, Stream)
│   │   ├── core/              # Core functionality (memory, metrics)
│   │   ├── network/           # Networking layer (RESP protocol)
│   │   ├── server/            # Server core (Server, Shard)
│   │   ├── persistence/       # Persistence layer (AOF, RDB, ROCKSDB)
│   │   ├── cluster/           # Cluster management (Gossip)
│   │   ├── security/          # Security layer (ACL)
│   │   ├── replication/       # Replication manager
│   │   └── storage/           # Storage utilities
│   └── main.cpp               # Application entry point
├── tests/
│   ├── unit/                  # Unit tests
│   ├── benchmark/             # Performance benchmarks
│   └── integration/           # Integration tests
├── cmake/                     # CMake configuration
├── third_party/               # Third-party dependencies
├── AstraDB_DESIGN.md          # Design documentation
├── README.md                  # This file
├── LICENSE                    # Apache 2.0 License
└── CMakeLists.txt             # Root CMake file
```

## 🚀 Quick Start

### Prerequisites

- CMake 3.20+
- C++23 compatible compiler (GCC 13+, Clang 16+)
- Ninja build system (recommended)

### Build

```bash
# Clone the repository
git clone https://github.com/caomengxuan666/AstraDB.git
cd AstraDB

# Configure and build (Release mode)
cmake -B build-release -G "Ninja" -DCMAKE_BUILD_TYPE=Release
ninja -C build-release

# Run the server
./build-release/bin/astradb --port 6379
```

### Build Options

```bash
# Debug mode
cmake -B build-debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
ninja -C build-debug

# Release mode with debug information (recommended for development)
cmake --preset linux-release-debuginfo-clang
ninja -C build-linux-release-debuginfo-clang

# Enable LTO (Link-Time Optimization)
cmake -B build-release -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DASTRADB_ENABLE_LTO=ON
ninja -C build-release

# Enable SIMD optimizations (default: ON)
cmake -B build-release -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DASTRADB_ENABLE_SIMD=ON
ninja -C build-release

# Build without examples (for package builds)
cmake -B build-release -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DASTRADB_BUILD_EXAMPLES=OFF
ninja -C build-release

# Use io_uring backend (Linux 5.1+, high performance)
cmake --preset linux-package-clang-iouring

# Use epoll backend (stable, compatible with all Linux versions)
cmake --preset linux-package-clang
```

### Configuration

You can configure AstraDB using command-line options or a configuration file:

```bash
# Command-line options
./build-release/bin/astradb \
  --port 6379 \
  --threads 20 \
  --shards 16 \
  --databases 16 \
  --max-connections 10000

# Single-threaded mode (for testing or low-load scenarios)
./build-release/bin/astradb \
  --port 6379 \
  --threads 1 \
  --shards 1

# Using configuration file
./build-release/bin/astradb --config astradb.toml
```

Example `astradb.toml`:

```toml
[server]
host = "0.0.0.0"
port = 6379
max_connections = 10000
thread_count = 20
shard_count = 16
database_count = 16

[logging]
level = "info"
async = true
queue_size = 8192

[persistence]
aof_enabled = true
rdb_enabled = true
snapshot_interval = 300  # seconds
```

## 🧪 Testing

### Run Tests

```bash
# Run unit tests
./build-release/bin/astradb_tests

# Run specific test suite
./build-release/bin/astradb_tests --gtest_filter=StringCommandsTest.*

# Run benchmarks
./build-release/bin/astradb_benchmarks

# Run specific benchmark
./build-release/bin/astradb_benchmarks --benchmark_filter=SetPerformance
```

### Redis Compatibility Testing

```bash
# Using redis-cli (from Redis)
redis-cli -p 6379 PING

# Test basic operations
redis-cli -p 6379 SET mykey "Hello, AstraDB!"
redis-cli -p 6379 GET mykey

# Test list operations
redis-cli -p 6379 LPUSH mylist item1 item2 item3
redis-cli -p 6379 LRANGE mylist 0 -1

# Test sorted set operations
redis-cli -p 6379 ZADD myzset 1 "one" 2 "two" 3 "three"
redis-cli -p 6379 ZRANGE myzset 0 -1 WITHSCORES
```

### Benchmarking

```bash
# Using redis-benchmark (from Redis)
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 100000 -c 50

# Benchmark specific commands
redis-benchmark -h 127.0.0.1 -p 6379 -t lpush,lpop,rpush,rpop -n 100000 -c 50

# Benchmark with pipeline
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 100000 -c 50 -P 16
```

## 📚 Documentation

- **Design Document**: [AstraDB_DESIGN.md](AstraDB_DESIGN.md) - Comprehensive design and architecture documentation
- **Performance Document**: [PERFORMANCE.md](PERFORMANCE.md) - Performance benchmarks and optimizations
- **Eviction Strategy Document**: [DOCS/eviction-strategy-optimization.md](DOCS/eviction-strategy-optimization.md) - Dragonfly-style 2Q eviction algorithm

## 🤝 Contributing

Contributions are welcome! Please follow these guidelines:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

### Coding Standards

- Follow the existing code style
- Use C++23 features where appropriate
- Add unit tests for new features
- Update documentation as needed
- Run `clang-format` and `clang-tidy` before submitting

### Commit Guidelines

- Use clear, descriptive commit messages
- Follow conventional commit format:
  - `feat: add new feature`
  - `fix: fix bug`
  - `docs: update documentation`
  - `test: add tests`
  - `refactor: refactor code`
  - `perf: performance improvement`

## 🗺️ Roadmap

### v1.0.0 (Current Target)

- [x] Core server infrastructure
- [x] 250+ Redis commands (96%+ Redis 7.4.1 compatibility)
- [x] All data types (String, Hash, List, Set, ZSet, Stream)
- [x] Persistence (AOF, RDB, ROCKSDB)
- [x] Cluster support (Gossip)
- [x] ACL support
- [x] Lua scripting
- [x] Transactions
- [x] Pub/Sub
- [x] RESP2 protocol support
- [x] RESP3 protocol support (HELLO command)
- [x] Bitmap commands
- [x] HyperLogLog commands
- [x] Geospatial commands
- [x] TTL commands
- [ ] Real blocking commands (simplified implementation)
- [ ] Raft consensus
- [ ] Full Redis compatibility (100%)
- [ ] Comprehensive documentation

### v1.2.0 (Future)

- [ ] TLS encryption
- [ ] Full RESP3 protocol
- [ ] Vector search
- [ ] Redis Modules compatibility
- [ ] Web UI for monitoring
- [ ] Comprehensive documentation

### Implemented Commands (250+)

AstraDB has implemented **250+ Redis commands**, covering all major data types and features. We aim to achieve **100% Redis 7.4.1 compatibility**.

#### String Commands (30+)
- `GET`, `SET`, `DEL`, `EXISTS`, `MGET`, `MSET`, `MSETNX`
- `INCR`, `DECR`, `INCRBY`, `DECRBY`, `INCRBYFLOAT`
- `APPEND`, `STRLEN`, `GETRANGE`, `SETRANGE`, `SUBSTR`
- `SETEX`, `PSETEX`, `SETNX`, `GETSET`, `GETDEL`, `GETEX`
- `STRALGO`, `LCS`, `COPY`, `DUMP`, `RESTORE`, `UNLINK`
- `TYPE`, `ECHO`, `RANDOMKEY`, `RENAME`, `RENAMENX`, `MOVE`, `TOUCH`

#### Hash Commands (20+)
- `HSET`, `HGET`, `HGETALL`, `HKEYS`, `HVALS`, `HLEN`
- `HMGET`, `HMSET`, `HEXISTS`, `HDEL`
- `HINCRBY`, `HINCRBYFLOAT`, `HSTRLEN`, `HRANDFIELD`
- `HSCAN`, `HSETNX`, `HTTL`, `HEXPIRE`, `HEXPIREAT`
- `HEXPIRETIME`, `HPERSIST`, `HPEXPIRE`, `HPEXPIREAT`, `HPEXPIRETIME`, `HPTTL`

#### List Commands (20+)
- `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LPUSHX`, `RPUSHX`
- `LLEN`, `LRANGE`, `LINDEX`, `LINSERT`, `LSET`, `LTRIM`, `LREM`, `LPOS`
- `RPOPLPUSH`, `LMOVE`, `LMPOP`
- `BLPOP`, `BRPOP`, `BRPOPLPUSH`, `BLMOVE`, `BLMPOP`

#### Set Commands (20+)
- `SADD`, `SREM`, `SISMEMBER`, `SCARD`, `SMEMBERS`
- `SRANDMEMBER`, `SPOP`, `SMOVE`, `SMISMEMBER`
- `SDIFF`, `SINTER`, `SUNION`, `SINTERCARD`
- `SDIFFSTORE`, `SINTERSTORE`, `SUNIONSTORE`
- `SSCAN`, `SORT`, `SORT_RO`
- `SPUBLISH`, `SSUBSCRIBE`, `SUNSUBSCRIBE`

#### Sorted Set Commands (30+)
- `ZADD`, `ZREM`, `ZCARD`, `ZSCORE`, `ZINCRBY`, `ZCOUNT`, `ZMSCORE`, `ZRANDMEMBER`
- `ZRANGE`, `ZREVRANGE`, `ZRANGEBYSCORE`, `ZREVRANGEBYSCORE`, `ZRANGESTORE`
- `ZRANK`, `ZREVRANK`, `ZPOPMIN`, `ZPOPMAX`
- `ZRANGEBYLEX`, `ZREVRANGEBYLEX`, `ZLEXCOUNT`, `ZREMRANGEBYLEX`
- `ZREMRANGEBYRANK`, `ZREMRANGEBYSCORE`
- `ZUNION`, `ZINTER`, `ZUNIONSTORE`, `ZINTERSTORE`, `ZDIFF`, `ZDIFFSTORE`, `ZINTERCARD`
- `ZMPOP`, `BZPOPMIN`, `BZPOPMAX`, `BZMPOP`
- `ZSCAN`

#### Stream Commands (15+)
- `XADD`, `XREAD`, `XRANGE`, `XREVRANGE`, `XREADGROUP`
- `XLEN`, `XDEL`, `XTRIM`, `XSETID`
- `XGROUP`, `XACK`, `XPENDING`, `XINFO`, `XCLAIM`, `XAUTOCLAIM`

#### Transaction Commands (5+)
- `MULTI`, `EXEC`, `DISCARD`, `WATCH`, `UNWATCH`

#### Pub/Sub Commands (5+)
- `SUBSCRIBE`, `UNSUBSCRIBE`, `PSUBSCRIBE`, `PUNSUBSCRIBE`, `PUBLISH`

#### Script Commands (5+)
- `EVAL`, `EVALSHA`, `SCRIPT`, `SCRIPT EXISTS`, `SCRIPT FLUSH`

#### Admin Commands (40+)
- `INFO`, `CONFIG`, `DBSIZE`, `KEYS`, `FLUSHDB`, `FLUSHALL`, `SELECT`
- `PING`, `ECHO`, `QUIT`, `SAVE`, `BGSAVE`, `LASTSAVE`, `BGREWRITEAOF`
- `COMMAND`, `DEBUG`, `CLUSTER`, `MIGRATE`, `MODULE`, `SCAN`, `MEMORY`, `ASKING`
- `TYPE`, `RANDOMKEY`, `RENAME`, `RENAMENX`, `MOVE`, `OBJECT`, `TOUCH`
- `TIME`, `SHUTDOWN`, `SWAPDB`, `WAIT`, `WAITAOF`, `READONLY`, `READWRITE`, `RESET`
- `FAILOVER`, `LATENCY`, `MONITOR`, `SLOWLOG`, `LOLWUT`, `AUTH`, `ACL`, `HELLO`, `SLAVEOF`, `REPLICAOF`

#### ACL Commands (5+)
- `ACL SETUSER`, `ACL GETUSER`, `ACL DELUSER`, `ACL LIST`, `ACL USERS`

#### Bitmap Commands (5+)
- `SETBIT`, `GETBIT`, `BITCOUNT`, `BITPOS`, `BITOP`

#### HyperLogLog Commands (5+)
- `PFADD`, `PFCOUNT`, `PFMERGE`, `PFDEBUG`, `PFSELFTEST`

#### Geospatial Commands (10+)
- `GEOADD`, `GEODIST`, `GEOHASH`, `GEOPOS`, `GEORADIUS`, `GEORADIUSBYMEMBER`

#### Client Commands (5+)
- `CLIENT`, `CLIENT LIST`, `CLIENT KILL`, `CLIENT SETNAME`, `CLIENT GETNAME`

#### Cluster Commands (5+)
- `CLUSTER`, `CLUSTER INFO`, `CLUSTER NODES`, `CLUSTER MEET`, `CLUSTER SLOTS`

#### Replication Commands (5+)
- `SYNC`, `PSYNC`, `REPLCONF`, `SLAVEOF`, `REPLICAOF`, `ROLE`

#### TTL Commands (9+)
- `TTL`, `PTTL`, `EXPIRE`, `PEXPIRE`, `EXPIREAT`, `PEXPIREAT`
- `EXPIRETIME`, `PEXPIRETIME`, `PERSIST`

### Command Completion Status

| Category | Commands | Status |
|----------|----------|--------|
| String | 30+ | ✅ Complete |
| Hash | 20+ | ✅ Complete |
| List | 20+ | ✅ Complete |
| Set | 20+ | ✅ Complete |
| Sorted Set | 30+ | ✅ Complete |
| Stream | 15+ | ✅ Complete |
| Transaction | 5 | ✅ Complete |
| Pub/Sub | 6 | ✅ Complete |
| Script | 9 | ✅ Complete |
| Admin | 40+ | ✅ Complete |
| ACL | 5+ | ✅ Complete |
| Bitmap | 6 | ✅ Complete |
| HyperLogLog | 5 | ✅ Complete |
| Geospatial | 10+ | ✅ Complete |
| Client | 4 | ✅ Complete |
| Cluster | 5+ | ✅ Complete |
| Replication | 6 | ✅ Complete |
| TTL | 9 | ✅ Complete |
| **Total** | **250+** | **96%+** |

### Command Implementation Notes

- **Full Implementation**: Commands with complete Redis 7.4.1 compatibility
- **Simplified Implementation**: Commands with basic functionality, marked for future enhancement
- **Experimental**: Commands with partial support or in development

All 250+ commands are registered and functional. We aim for **100% Redis 7.4.1 compatibility** by v1.0.0.

## 📈 Performance Targets

| Metric | Current | Target | Status |
|--------|---------|--------|--------|
| GET QPS | 62 Kops/s | 1M ops/s | 6.2% |
| SET QPS | 63 Kops/s | 800 Kops/s | 7.9% |
| Command Coverage | 250+ (96%+) | 250+ (100%) | 96% |
| Memory Overhead | TBD | 0 bytes | TBD |
| Scalability (1→8 threads) | TBD | 8x | TBD |
| Startup Time | TBD | 0.5s | TBD |

## 🏆 Comparison

### AstraDB vs Redis vs DragonflyDB

| Feature | Redis | DragonflyDB | AstraDB |
|---------|-------|-------------|---------|
| Single-threaded | ✅ | ❌ | ❌ |
| Multi-threaded | ❌ | ✅ | ✅ |
| Sharding | Manual | Automatic | Automatic |
| Persistence | RDB/AOF | Snapshot | AOF/RDB/ROCKSDB |
| Clustering | Redis Cluster | Built-in | Built-in |
| ACL | ✅ | ✅ | ✅ |
| Lua Scripting | ✅ | ✅ | ✅ |
| Transactions | ✅ | ✅ | ✅ |
| Pub/Sub | ✅ | ✅ | ✅ |
| Streams | ✅ | ✅ | ✅ |
| HyperLogLog | ✅ | ✅ | ✅ |
| Geospatial | ✅ | ✅ | ✅ |
| Bitmaps | ✅ | ✅ | ✅ |
| SIMD | ❌ | ✅ | ✅ |
| MPSC Queues | ❌ | ✅ | ✅ |
| Zero-Copy I/O | ❌ | ✅ | ✅ |
| C++23 | ❌ | ❌ | ✅ |

## 🔐 Security

### Access Control List (ACL)

AstraDB supports Redis-compatible ACL for user-based access control:

```bash
# Create a new user
ACL SETUSER myuser on >mypassword ~* +@all

# Get user information
ACL GETUSER myuser

# List all users
ACL LIST

# Delete a user
ACL DELUSER myuser
```

### TLS Support (Planned)

TLS encryption support is planned for v1.1.0:

```bash
# Enable TLS in configuration
[server]
tls_enabled = true
tls_cert_file = "/path/to/cert.pem"
tls_key_file = "/path/to/key.pem"
```

## 📞 Support

- **GitHub Issues**: [https://github.com/caomengxuan666/AstraDB/issues](https://github.com/caomengxuan666/AstraDB/issues)
- **Discussions**: [https://github.com/caomengxuan666/AstraDB/discussions](https://github.com/caomengxuan666/AstraDB/discussions)
- **Documentation**: [AstraDB_DESIGN.md](AstraDB_DESIGN.md)

## 📄 License

Licensed under the Apache License, Version 2.0. See the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- **Redis**: The inspiration and protocol reference
- **DragonflyDB**: Performance optimization ideas
- **Asio**: Excellent async networking library
- **FlatBuffers**: Zero-copy serialization
- **ROCKSDB**: Lightweight key-value store
- **libgossip**: Gossip protocol implementation
- **spdlog**: High-performance logging
- **mimalloc**: Fast memory allocator
- **All contributors**: Thank you for your contributions!

---

**Let's build the fastest Redis-compatible database! 🚀**