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
- **Vector Search**: In-memory ANN search with hnswlib (cosine/L2/IP)
- **Rich Commands**: Support for 260+ Redis commands across all data types
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
- **Scatter-Gather VSEARCH**: Parallel multi-worker vector search
- **Redis Modules compatibility**: Support for Redis Modules API

## 📊 Performance

### Current Baseline (2026-04-23)

All numbers below are from a single mixed benchmark mode (`-t set,get`) to avoid split-test bias.

**Test setup:**
- **Binary**: `build-linux-release-debuginfo-noasan/bin/astradb`
- **Config**: `config/astradb-benchmark.toml` (2 workers, 2 shards, persistence disabled)
- **Client**: `redis-benchmark`
- **Command mix**: `-t set,get`
- **Run shape**: `-n 1000000 -c 256 -P <pipeline>`

| Pipeline (`-P`) | SET QPS | GET QPS |
|-----------------|---------|---------|
| **1** | 209,424.08 | 209,117.52 |
| **16** | 1,381,215.50 | 1,805,054.12 |
| **64** | 1,612,903.25 | 2,145,922.75 |

**Repeatability checks (same machine, same config):**
- `P=1`: SET `210,837.02`, GET `209,511.84`
- `P=64`: SET `1,600,000.00`, GET `2,127,659.50`

For historical investigation notes and older experiments, see `PERFORMANCE.md`.

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
│  │  Stream Data | HNSW Vector Index | FlatBuffers Serialize   │  │
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
| **Vector Search** | hnswlib | 0.8.0 | HNSW approximate nearest neighbor |
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

# Storage mode configuration
[storage]
mode = "redis"  # Options: "redis" (default) or "rocksdb"

# Redis mode persistence (when mode = "redis")
[persistence]
aof_enabled = true
rdb_enabled = true
snapshot_interval = 300  # seconds

# RocksDB configuration (when mode = "rocksdb")
[rocksdb]
data_dir = "./data/rocksdb_allin"
# All data is persisted to RocksDB in all-in mode
# This is different from Redis mode where RocksDB is only used for cold data
```

### Storage Modes

AstraDB supports two storage modes for different use cases:

#### Redis Mode (Default)
- **Memory-first**: Data primarily stored in memory
- **Persistence**: AOF and RDB for durability
- **Cold Data Storage**: RocksDB for evicted data (optional)
- **Use Case**: High-performance scenarios with low latency requirements

```toml
[storage]
mode = "redis"

[persistence]
aof_enabled = true
rdb_enabled = true
rocksdb_cold_data = true  # Optional: enable RocksDB for cold data
```

#### RocksDB All-in Mode
- **Disk-first**: All data persisted to RocksDB
- **Memory Cache**: Hot data cached in memory with automatic eviction
- **Persistence**: FlatBuffer serialization for type-safe storage
- **Use Case**: Large datasets, memory-constrained environments

```toml
[storage]
mode = "rocksdb"

[rocksdb]
data_dir = "./data/rocksdb_allin"
```

**Key Differences**:
- **Redis Mode**: Best for performance, lower latency, requires more memory
- **RocksDB All-in Mode**: Best for memory efficiency, handles large datasets, slightly higher latency

Both modes support all Redis data types and commands with automatic persistence.

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

### Vector Search Quick Start 🆕

```bash
# Start AstraDB
./build-release/bin/astradb --port 6379

# Use the bundled Python benchmark script
python3 scripts/vector_bench.py --dim 512 --count 5000 --search-qps-duration 10

# Or test manually with raw RESP commands:
# Create a 512-dim cosine index
printf '*4\r\n$7\r\nVCREATE\r\n$2\r\ndb\r\n$3\r\n512\r\n$6\r\ncosine\r\n' | nc 127.0.0.1 6379

# Search with a query vector (requires raw float32 bytes)
```

```python
# Python example using raw sockets
import socket, struct, random

def resp_cmd(sock, *args):
    parts = [f'*{len(args)}\r\n'.encode()]
    for a in args:
        b = a if isinstance(a, bytes) else a.encode()
        parts.append(f'${len(b)}\r\n'.encode() + b + b'\r\n')
    sock.sendall(b''.join(parts))
    return sock.recv(1024)

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 6379))

# Create index
resp_cmd(sock, 'VCREATE', 'docs', '768', 'cosine')

# Insert a vector (768-dimensional random)
vec = struct.pack('768f', *[random.gauss(0, 1) for _ in range(768)])
resp_cmd(sock, 'VSET', 'docs', 'doc:1', vec)

# Search top-10
query = struct.pack('768f', *[random.gauss(0, 1) for _ in range(768)])
result = resp_cmd(sock, 'VSEARCH', 'docs', query, '10')
print(result)
```

### Benchmarking

```bash
# Canonical mixed read/write benchmark (recommended)
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 256 -P 1 -q
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 256 -P 16 -q
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 256 -P 64 -q

# Optional: command-specific stress
redis-benchmark -h 127.0.0.1 -p 6379 -t lpush,lpop,rpush,rpop -n 1000000 -c 256 -P 16 -q
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
- [x] Vector search (in-memory, hnswlib, cosine/L2/IP)

### v1.2.0 (Future)

- [ ] TLS encryption
- [ ] Full RESP3 protocol
- [ ] Scatter-gather multi-worker VSEARCH
- [ ] Redis Modules compatibility
- [ ] Web UI for monitoring

### Implemented Commands (250+)

AstraDB has implemented **260+ Redis commands**, covering all major data types and features. We aim to achieve **100% Redis 7.4.1 compatibility**.

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

#### Vector Search Commands (10) 🆕
- `VCREATE` — Create a vector index with dimension and distance metric
- `VDROP` — Drop a vector index
- `VLIST` — List all vector indexes
- `VSET` — Insert/update a vector with optional metadata
- `MVSET` — Batch insert multiple vectors
- `VGET` — Retrieve a vector entry
- `VDEL` — Delete a vector entry
- `VSEARCH` — KNN search by vector similarity
- `VINFO` — Get vector index statistics
- `VCOMPACT` — Compact vector index (remove deleted entries)

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
| Vector Search | 10 | ✅ New |
| **Total** | **260+** | **96%+** |

### Command Implementation Notes

- **Full Implementation**: Commands with complete Redis 7.4.1 compatibility
- **Simplified Implementation**: Commands with basic functionality, marked for future enhancement
- **Experimental**: Commands with partial support or in development

All 250+ commands are registered and functional. We aim for **100% Redis 7.4.1 compatibility** by v1.0.0.

## 📈 Performance Metrics

| Metric | Current | Target | Status |
|--------|---------|--------|--------|
| GET QPS (Non-Pipeline) | 220k+ | 250k+ | ✅ **88% achieved** |
| SET QPS (Non-Pipeline) | 220k+ | 250k+ | ✅ **88% achieved** |
| Pipeline QPS | ~several million | 10M+ | ⏳ Testing planned |
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
| Vector Search | ✅ (RediSearch) | ❌ | ✅ (hnswlib) |

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
