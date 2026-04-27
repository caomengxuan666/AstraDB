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
| Perf Optimizations | 5 | 1 week |
| Testing | 6 | 2 weeks |
| Documentation | 3 | 1 week |
| **Total** | **70** | **~12–16 weeks** (est.) |

### Suggested Execution Order

1. **RDB File Naming Fix** — critical, <1 day, unblocks multi-slave
2. **Real-time Command Stream Replication** — critical, makes replication functional
3. **REPLCONF ACK / Backlog** — critical, prerequisite for partial sync
4. **Partial Sync (CONTINUE)** — high, reduces reconnect cost
5. **Blocking Commands** — critical, major user-facing gap
6. **Slave Read-Only Mode** — medium, production safety
7. **Replication Timeout & Auto-Reconnect** — medium, robustness
8. **RocksDB Serialization** — medium, RocksDB all-in completeness
9. **Testing & Validation** — run in parallel throughout
10. **Everything else** (ASTRA commands, ACL, TLS, docs) — lower priority
