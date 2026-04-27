# AstraDB — Remaining Work (TODO)

**Last Updated**: 2026-04-28  
**Version**: 1.5.3  
**Overall Completion**: ~94% (single-node), ~73% (multi-node)

---

## 🔴 Blocking Commands (Real Implementation)

Status: **Simplified stubs only** — returns nil immediately when keys are empty.  
Source TODOs: `list_commands.cpp` (16), `zset_commands.cpp` (9)  
Effort: **2–3 weeks**

| # | Task | Priority |
|---|------|----------|
| 1 | Implement real blocking wait queues with timeout management | Critical |
| 2 | BLPOP, BRPOP, BRPOPLPUSH — full blocking with expiration | Critical |
| 3 | BLMPOP, BLMOVE, BLMPOP, BZPOPMIN, BZPOPMAX — full blocking | Critical |
| 4 | Wire up `BlockingManager` with async notification (key-space events) | Critical |
| 5 | Integration tests for blocking commands (timeout, wake-up, correctness) | High |

---

## 🔴 Replication — Real-time Command Stream

Status: RDB snapshot transmission works; live command propagation is **NOT implemented**. Data written after initial sync is not replicated.  
Effort: **1–2 weeks**

| # | Task | Priority |
|---|------|----------|
| 1 | Implement `PropagateCommand()` — serialize and transmit executed commands to all connected slaves | Critical |
| 2 | RESP serialization/deserialization of command stream on the wire | Critical |
| 3 | Command buffer management (batching, flush strategies) | High |
| 4 | Error handling: partial sends, reconnect, resync | High |
| 5 | End-to-end test: write on master → read on slave continuously | High |

---

## 🔴 Replication — RDB File Naming Fix

Status: **NO SHARING architecture violation** — all workers use the same `dump_sync.rdb` filename.  
Effort: **2–3 hours**

| # | Task | Priority |
|---|------|----------|
| 1 | Include `worker_id` and `connection_id` in RDB dump filename (e.g. `dump_sync_w0_c3.rdb`) | Critical |
| 2 | Validate with multi-slave setup — no file corruption or race conditions | High |

---

## 🔴 Replication — REPLCONF ACK / Backlog

Status: Not started.  
Effort: **1 week**

| # | Task | Priority |
|---|------|----------|
| 1 | Slave sends periodic offset acknowledgements back to master | Critical |
| 2 | Master tracks replication lag per slave (offset map) | High |
| 3 | Replication backlog buffer — ring-buffer storage of recent commands | High |
| 4 | Backlog eviction (size limit, head-truncation) | High |
| 5 | Expose backlog/ACK stats via `INFO replication` and Prometheus | Medium |

---

## 🟠 Replication — Partial Sync (CONTINUE)

Status: Only FULLRESYNC works; offset-based incremental sync not implemented.  
Effort: **1 week**

| # | Task | Priority |
|---|------|----------|
| 1 | Check whether slave's replication offset falls within master's backlog range | High |
| 2 | Extract and send delta commands from backlog instead of full RDB snapshot | High |
| 3 | RESP protocol: `+CONTINUE` response with correct replid + offset | High |
| 4 | Integration test: disconnect → reconnect → partial sync | High |

---

## 🟠 Replication — Slave Read-Only Mode

Status: Config parameter `read_only` exists but is not enforced.  
Effort: **2–3 days**

| # | Task | Priority |
|---|------|----------|
| 1 | Block write commands on slaves when `read_only=true` | Medium |
| 2 | Maintain a write-command denylist (SET, DEL, HSET, etc.) | Medium |
| 3 | Support runtime toggle via `REPLCONF` | Low |

---

## 🟠 Replication — Timeout & Auto-Reconnect

Status: Not implemented.  
Effort: **3–4 days**

| # | Task | Priority |
|---|------|----------|
| 1 | Detect master-slave connection loss (heartbeat / timeout) | Medium |
| 2 | Auto-reconnect with exponential backoff | Medium |
| 3 | Attempt partial sync on reconnect; fall back to full sync | Medium |
| 4 | Connection state machine (connecting → connected → disconnected → reconnecting) | Low |

---

## 🟠 Replication — Master-Slave Failover

Status: Not started.  
Effort: **1–2 weeks**

| # | Task | Priority |
|---|------|----------|
| 1 | Detect master failure via gossip | Low |
| 2 | Slave promotion to master (controlled failover) | Low |
| 3 | Other slaves reconnect to the new master | Low |
| 4 | CLUSTER FAILOVER command | Low |

---

## 🟡 RocksDB Serialization Gaps

Status: String keys work; ZSet, List, Stream data structures cannot be serialized for RocksDB all-in mode.  
Source TODOs: `database.hpp` (3)  
Effort: **1 week**

| # | Task | Priority |
|---|------|----------|
| 1 | ZSet → RocksDB key-value layout | Medium |
| 2 | List → RocksDB key-value layout | Medium |
| 3 | Stream → RocksDB key-value layout | Medium |
| 4 | `ASTRA.STORAGE.INFO` — real stats from RocksDB (SST files, block cache, memtable size) | Low |

---

## 🟡 ASTRA Custom Commands

Status: Several commands are stubs with TODOs.  
Source TODOs: `astra_commands.cpp` (8), `admin_commands.cpp` (7)

| # | Task | Priority |
|---|------|----------|
| 1 | `ASTRA.STORAGE.COMPACT` — trigger RocksDB compaction | Medium |
| 2 | `ASTRA.STORAGE.PERF` — per-database I/O and cache stats | Medium |
| 3 | `ASTRA.STORAGE.MEMORY` — actual heap/RSS reporting | Medium |
| 4 | `ASTRA.MIGRATE` — live key migration between nodes | Low |
| 5 | `INFO replication` — real master/slave/offset data | Medium |
| 6 | `COPY` / `REPLACE` key commands | Low |

---

## 🟡 Lua Scripting Gaps

Source TODOs: `script_commands.cpp` (2)

| # | Task | Priority |
|---|------|----------|
| 1 | Resolve worker_id from execution context inside script calls | Medium |
| 2 | `SCRIPT DEBUG` — full debugging support | Low |
| 3 | Script sandbox isolation & resource limits (CPU, memory) | Low |
| 4 | LuaJIT integration for performance | Low |
| 5 | Script replication across masters/slaves | Low |

---

## 🟡 ACL Integration

Status: `AclManager` class exists but is **NOT wired** into the server. No authentication.  
Effort: **1 week**

| # | Task | Priority |
|---|------|----------|
| 1 | Integrate `AclManager` into connection/command pipeline | Medium |
| 2 | `AUTH` command | Medium |
| 3 | `ACL SETUSER`, `ACL DELUSER`, `ACL GETUSER`, `ACL LIST`, `ACL WHOAMI` | Medium |
| 4 | `ACL CAT`, `ACL GENPASS` | Low |
| 5 | Permission checking: command allow/deny, key prefix, pub/sub channels | Medium |
| 6 | Load ACL rules from config file | Low |

---

## 🟡 TLS Encryption

Status: Configuration option present but disabled.  
Effort: **1–2 weeks**

| # | Task | Priority |
|---|------|----------|
| 1 | TLS acceptor via Asio SSL streams | Low |
| 2 | Certificate/key file loading from config | Low |
| 3 | `REQUIREPASS` / `requirepass` config support | Low |

---

## 🟢 Performance Optimizations & Polish

| # | Task | Priority |
|---|------|----------|
| 1 | B+ Tree rebalancing optimization (`bplustree_internal.hpp` — 4 TODOs) | Low |
| 2 | Refactor `void*` in worker to typed dispatch (`worker.hpp` — 2 TODOs) | Low |
| 3 | `StringAppendOperator` for RocksDB append operations | Low |
| 4 | Data serializer: Stream type support (`data_serializer.hpp`) | Low |
| 5 | Remove hardcoded IP:port in replication commands (`replication_commands.cpp` — 5 TODOs) | Medium |

---

## 🟢 Testing & Validation

| # | Task | Priority |
|---|------|----------|
| 1 | End-to-end replication test suite (full sync + command stream + partial sync + failover) | High |
| 2 | RDB reader: fix type-byte parsing edge cases (`rdb_test.cpp` — 2 TODOs) | Medium |
| 3 | RDB reader: fix large value test (`rdb_test.cpp`) | Medium |
| 4 | Future/async primitives unit tests (`future_test.cpp` — disabled) | Low |
| 5 | Stress/chaos testing — network partitions, high concurrency, memory pressure | Medium |
| 6 | Performance benchmarks — measure P99 latency, throughput, linear scaling 1–8 threads | Medium |

---

## 🟡 Vector Search (In-Memory, hnswlib-backed)

Status: **Not started**.  
Architecture: FlatBuffers internal + RESP external. Vectors as 8th data type in `Database`. RocksDB-only persistence (no AOF). NO SHARING compliant.  
Backend: hnswlib (MIT, header-only, ~3K lines). SIMD acceleration via existing `simd_utils.hpp`.  
Effort: **6–8 weeks**

### Architecture Decisions

| Decision | Rationale |
|----------|-----------|
| **Internal FlatBuffers, external RESP** | Consistent with existing AOF/RDB patterns; zero-copy serialization for vector blobs |
| **Vectors in Database as 8th data type** | Follows existing String/Hash/ZSet pattern; reuses KeyMetadata, MemoryTracker, EvictionManager, batch callback |
| **No separate KV/DB layer** | Database is already both KV store and DB layer; no benefit to splitting |
| **NO SHARING: hash-slot routing** | VSET/VGET/VDEL route by key hash (identical to SET pattern) |
| **NO SHARING: scatter-gather search** | VSEARCH broadcasts via CrossWorkerRequest → each worker searches local HNSW → priority-queue merge (identical to CLIENT LIST pattern) |
| **RocksDB-only persistence** | No AOF for vectors — too verbose for float arrays. Vectors stored as RocksDB keys; HNSW rebuilt from RocksDB on restart |
| **hnswlib, not FAISS** | Header-only, MIT license, zero build complexity, sufficient for million-scale |
| **Post-filter metadata strategy** | Search with 2×k candidates, then apply filter conditions in-memory |

### Command Set (10 commands)

```
VCREATE index DIM n DISTANCE cosine|l2|ip [M m] [EF ef]
VDROP index
VLIST [IDX]
VSET index key vector [META k v ...]
MVSET index key1 vec1 [META ...] key2 vec2 [META ...] ...
VGET index key [VECTOR] [META]
VDEL index key
VSEARCH index vector k [FILTER field op val ...] [WITHVECTOR] [WITHMETA] [WITHSCORE]
VINFO index
VCOMPACT index
```

### Phase 1: Core Engine (3–5 days)

| # | Task | Priority |
|---|------|----------|
| 1 | Integrate hnswlib via CPM (header-only, compile flag) | Critical |
| 2 | Add `KeyType::kVector` to `key_metadata.hpp` | Critical |
| 3 | Implement `VectorEntry` struct (vector bytes + metadata map) | Critical |
| 4 | Implement `VectorIndexManager` class wrapping hnswlib per-index instances | Critical |
| 5 | SIMD distance functions: COSINE, L2, IP using existing `simd_utils.hpp` (SSE2/AVX2/NEON) | High |
| 6 | Vector validation (dimension check, NaN/Inf guard) | High |

### Phase 2: Database Integration (2–3 days)

| # | Task | Priority |
|---|------|----------|
| 1 | Add `vectors_` DashMap to `Database` class | Critical |
| 2 | Add `VectorIndexManager` member to `Database` | Critical |
| 3 | Wire VSET → key registration + metadata + HNSW insert + memory tracking | Critical |
| 4 | Wire VDEL → cleanup from DashMap + HNSW mark-delete + RocksDB delete | Critical |
| 5 | Wire EvictKey for kVector → RocksDB save + DashMap remove + HNSW mark-delete | High |
| 6 | Extend `PersistKey()` with `kVector` case (FlatBuffers serialization to RocksDB) | High |

### Phase 3: Commands Implementation (1–2 weeks)

| # | Task | Priority |
|---|------|----------|
| 1 | VCREATE / VDROP / VLIST — index lifecycle | Critical |
| 2 | VSET / VGET / VDEL — single-key ops (hash-slot routed) | Critical |
| 3 | MVSET — batch insert (per-key routing, no broadcast needed) | Critical |
| 4 | VSEARCH — scatter-gather via CrossWorkerRequest + priority-queue merge | Critical |
| 5 | Post-filter expression parser (`field op value` with AND combination) | High |
| 6 | VINFO — index stats (count, dimension, memory, ef_construction) | High |
| 7 | VCOMPACT — hnswlib mark-delete cleanup + optional RocksDB checkpoint snapshot | Medium |
| 8 | Register all 10 commands in `RuntimeCommandRegistry` | Critical |
| 9 | `RoutingStrategy::kNone` for VSEARCH (global broadcast, no slot routing) | Critical |

### Phase 4: Persistence & Recovery (3–5 days)

| # | Task | Priority |
|---|------|----------|
| 1 | FlatBuffers schema for `VectorValue` (key, dimension, float32 blob, metadata fields) | Critical |
| 2 | `RocksDBSerializer::SerializeVector` / `DeserializeVector` | Critical |
| 3 | Startup recovery: scan RocksDB for all kVector keys → group by index_name → bulk rebuild HNSW | Critical |
| 4 | Optional `VCOMPACT` HNSW checkpoint snapshot to RocksDB (for faster restart) | Low |
| 5 | RDB snapshot support: include vector data in BGSAVE/SAVE output | Medium |

### Phase 5: Memory Management (2–3 days)

| # | Task | Priority |
|---|------|----------|
| 1 | Per-index memory tracking (register with existing `MemoryTracker`) | High |
| 2 | `vector.max_memory` config option in TOML | High |
| 3 | Vector eviction on memory pressure (LRU remove oldest entries, + HNSW mark-delete) | High |
| 4 | Object size estimator for vector entries (dim × sizeof(float) + metadata overhead + HNSW graph edge allocation) | Medium |

### Phase 6: Monitoring (1–2 days)

| # | Task | Priority |
|---|------|----------|
| 1 | Prometheus metrics: `vector_index_count`, `vector_total`, `vector_memory_bytes` | High |
| 2 | Prometheus metrics: `vector_search_latency_p50/p95/p99`, `vector_search_qps` | Medium |
| 3 | Prometheus metrics: `vector_insert_qps`, `index_dimension`, `index_distance` | Medium |

### Phase 7: Python Client & AI Ecosystem (3–5 days)

| # | Task | Priority |
|---|------|----------|
| 1 | Python package `astradb-vector`: RESP-based client for all 10 commands | High |
| 2 | LangChain `VectorStore` adapter class (~100 lines: `add_texts`, `similarity_search`, `delete`) | High |
| 3 | LlamaIndex adapter (optional, same pattern) | Low |
| 4 | Publish to PyPI | Low |
| 5 | Basic usage example: RAG pipeline with OpenAI/HuggingFace embedding | Medium |

### Phase 8: Testing (3–5 days)

| # | Task | Priority |
|---|------|----------|
| 1 | Unit tests: vector validation, distance functions, HNSW index lifecycle | High |
| 2 | Unit tests: expression parser correctness, edge cases | High |
| 3 | Integration tests: VSET/VSEARCH round-trip, multi-index scenarios | High |
| 4 | Integration tests: scatter-gather correctness (multi-worker) | High |
| 5 | Integration tests: persistence round-trip (RocksDB save → restart → rebuild → verify) | High |
| 6 | Benchmark: search latency vs vector count (10K/100K/1M scale) | Medium |
| 7 | Benchmark: batch insert throughput | Medium |

---

## 🟢 Documentation

| # | Task | Priority |
|---|------|----------|
| 1 | User guide / quick-start for single-node deployment | Low |
| 2 | Deployment guide for cluster + replication | Low |
| 3 | API/command reference (auto-generated from `COMMAND DOCS`) | Low |

---

## 📊 Summary

| Area | # of Tasks | Est. Effort |
|------|-----------|-------------|
| Blocking Commands | 5 | 2–3 weeks |
| Replication — Command Stream | 5 | 1–2 weeks |
| Replication — RDB Naming Fix | 2 | 2–3 hours |
| Replication — ACK / Backlog | 5 | 1 week |
| Replication — Partial Sync | 4 | 1 week |
| Replication — Read-Only Mode | 3 | 2–3 days |
| Replication — Timeout / Reconnect | 4 | 3–4 days |
| Replication — Failover | 4 | 1–2 weeks |
| RocksDB Serialization | 4 | 1 week |
| ASTRA Commands | 6 | 1 week |
| Lua Scripting | 5 | 2 weeks |
| ACL | 6 | 1 week |
| TLS | 3 | 1–2 weeks |
| Vector Search | 38 | 6–8 weeks |
| Perf Optimizations | 5 | 1 week |
| Testing | 6 | 2 weeks |
| Documentation | 3 | 1 week |
| **Total** | **108** | **~18–24 weeks** (est.) |

### Suggested Execution Order

1. **RDB File Naming Fix** — critical, <1 day, unblocks multi-slave
2. **Real-time Command Stream Replication** — critical, makes replication functional
3. **REPLCONF ACK / Backlog** — critical, prerequisite for partial sync
4. **Partial Sync (CONTINUE)** — high, reduces reconnect cost
5. **Blocking Commands** — critical, major user-facing gap
6. **Slave Read-Only Mode** — medium, production safety
7. **Replication Timeout & Auto-Reconnect** — medium, robustness
8. **RocksDB Serialization** — medium, RocksDB all-in completeness
9.  **RocksDB Serialization** — medium, RocksDB all-in completeness
10. **Vector Search — Phase 1+2 (Core + DB Integration)** — unblocks all vector work
11. **Vector Search — Phase 3 (Commands)** — makes vector search usable end-to-end
12. **Vector Search — Phase 4 (Persistence & Recovery)** — restart safety
13. **Vector Search — Phase 5+6+7+8 (Memory, Monitoring, Client, Testing)** — production readiness
14. **Everything else** (ASTRA commands, ACL, TLS, docs) — lower priority
