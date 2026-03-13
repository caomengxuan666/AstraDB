# AstraDB Coroutine Integration Plan

## Executive Summary

This document outlines the comprehensive plan for integrating C++20 coroutines (via Asio's `asio::awaitable`) into AstraDB's NO SHARING architecture. The goal is to improve code simplicity and maintainability while preserving the architectural principles that make AstraDB performant and scalable.

**Key Decision**: Coroutines will be used **ONLY** for I/O operations in the Connection class. Command execution will remain synchronous to preserve the NO SHARING architecture and avoid introducing global state.

## Table of Contents

1. [Current Architecture Overview](#current-architecture-overview)
2. [DragonflyDB Best Practices](#dragonflydb-best-practices)
3. [Asio Coroutine Fundamentals](#asio-coroutine-fundamentals)
4. [Integration Strategy](#integration-strategy)
5. [Implementation Plan](#implementation-plan)
6. [Testing Strategy](#testing-strategy)
7. [Performance Considerations](#performance-considerations)
8. [Risk Mitigation](#risk-mitigation)
9. [Success Criteria](#success-criteria)

---

## Current Architecture Overview

### NO SHARING Architecture

AstraDB's current architecture follows the NO SHARING principle:

```
┌─────────────────────────────────────────────────────────────────┐
│                         Server                                   │
├─────────────────────────────────────────────────────────────────┤
│  Worker 0              Worker 1              Worker N           │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐     │
│  │ IO Thread    │     │ IO Thread    │     │ IO Thread    │     │
│  │ - Acceptor   │     │ - Acceptor   │     │ - Acceptor   │     │
│  │ - Conn Pool  │     │ - Conn Pool  │     │ - Conn Pool  │     │
│  │ - io_context │     │ - io_context │     │ - io_context │     │
│  └──────┬───────┘     └──────┬───────┘     └──────┬───────┘     │
│         │                    │                    │              │
│         ▼                    ▼                    ▼              │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐     │
│  │ Executor     │     │ Executor     │     │ Executor     │     │
│  │ - Database   │     │ - Database   │     │ - Database   │     │
│  │ - Commands   │     │ - Commands   │     │ - Commands   │     │
│  │ - DataShard  │     │ - DataShard  │     │ - DataShard  │     │
│  └──────────────┘     └──────────────┘     └──────────────┘     │
└─────────────────────────────────────────────────────────────────┘
```

### Key Architectural Principles

1. **Per-Worker Isolation**: Each Worker is completely independent
2. **Private io_context**: Each Worker has its own Asio io_context
3. **Private Acceptor**: Each Worker has its own acceptor (SO_REUSEPORT)
4. **Private Data**: Each Worker has its own Database instance
5. **MPSC Communication**: Workers communicate via lock-free queues
6. **No Shared State**: Workers share no mutable state

### Current Connection Implementation

The current Connection class uses traditional callback-based async I/O:

```cpp
class Connection {
public:
  void Start() {
    DoRead();
  }

  void Send(const std::string& response) {
    asio::async_write(socket_, asio::buffer(response),
      [this](asio::error_code ec, size_t bytes) {
        // Handle write completion
      });
  }

private:
  void DoRead() {
    socket_.async_read_some(asio::buffer(buffer_),
      [this](asio::error_code ec, size_t bytes) {
        if (!ec) {
          receive_buffer_.append(buffer_.data(), bytes);
          ProcessCommands();
          DoRead();
        }
      });
  }
};
```

### Why This Architecture Works

- **No Locks Needed**: Each Worker operates on its own data
- **No Strand Needed**: Single thread per io_context
- **No Race Conditions**: No shared mutable state
- **Excellent Scalability**: Linear scaling with number of Workers

---

## DragonflyDB Best Practices

### DragonflyDB Architecture Deep Dive

Based on comprehensive analysis of DragonflyDB source code at `/home/cmx/codespace/dragonfly`:

#### Core Architecture Components

1. **Custom Fiber Implementation** (not C++20 coroutines):
   ```cpp
   // Dragonfly uses its own Fiber implementation
   class Fiber {
     boost::intrusive_ptr<detail::FiberInterface> impl_;
     // Fiber lifecycle management, yielding, sleeping, etc.
   };
   
   // Usage in PingConnection
   void HandleRequests() {
     AsioStreamAdapter<FiberSocketBase> asa(*peer);
     // Read loop - synchronous-looking code, actually fiber-aware
     size_t res = asa.read_some(mb, ec);
     if (FiberSocketBase::IsConnClosed(ec))
       break;
   }
   ```

2. **ProactorBase Architecture**:
   ```cpp
   class ProactorBase {
     pthread_t thread_id_;
     int32_t pool_index_;
     FuncQ task_queue_;  // mpmc_bounded_queue<Tasklet>
     
     // Each Proactor runs in its own thread
     void Run();  // Main I/O loop
     
     // Cross-proactor communication
     template <typename Func> bool DispatchBrief(Func&& f);
     template <typename Func> auto Await(Func&& f);
     
     virtual FiberSocketBase* CreateSocket() = 0;
   };
   ```

3. **FiberSocketBase - Socket Abstraction**:
   ```cpp
   class FiberSocketBase : public io::AsyncSink, 
                           public io::AsyncSource {
   protected:
     explicit FiberSocketBase(fb2::ProactorBase* pb) : proactor_(pb) {}
     
   public:
     // Synchronous-looking methods that use fibers internally
     virtual error_code Connect(const endpoint_type& ep, 
                               std::function<void(int)> on_pre_connect) = 0;
     virtual io::Result<size_t> Recv(const io::MutableBytes& mb, int flags = 0) = 0;
     
     ProactorBase* proactor() { return proactor_; }
   };
   
   // Implementation for io_uring
   class UringSocket : public LinuxSocketBase {
     Result<size_t> Recv(const io::MutableBytes& mb, int flags = 0) override {
       FiberCall fc(proactor, timeout());
       fc->PrepRecv(fd, mb.data(), mb.size(), flags);
       res = fc.Get();  // Yields fiber until I/O completes
       return res;
     }
   };
   ```

4. **Connection Management**:
   ```cpp
   class Connection : public boost::intrusive_ref_counter<Connection> {
     using connection_hook_t = boost::intrusive::list_member_hook<...>;
     connection_hook_t hook_;  // For intrusive list
     
   protected:
     std::unique_ptr<FiberSocketBase> socket_;
     virtual void HandleRequests() = 0;  // Main connection loop
     
   private:
     ListenerInterface* listener_ = nullptr;
   };
   
   // ListenerInterface manages connections
   class ListenerInterface {
     void RunAcceptLoop() {
       while (true) {
         auto res = sock_->Accept();  // Fiber-aware accept
         // Pick proactor for connection
         fb2::ProactorBase* next = PickConnectionProactor(peer.get());
         
         // Create connection and run in fiber
         Connection* conn = NewConnection(next);
         next->DispatchBrief([this, conn] {
           fb2::Fiber(fb2::Launch::post, fb2::FixedStackAllocator(mr_, stack_size),
                      "Connection", [this, conn] {
             RunSingleConnection(conn);
           }).Detach();
         });
       }
     }
     
     void RunSingleConnection(Connection* conn) {
       clist->Link(conn);
       OnConnectionStart(conn);
       try {
         conn->HandleRequests();  // Runs in fiber
       } catch (std::exception& e) {
         LOG(ERROR) << "Uncaught exception " << e.what();
       }
       OnConnectionClose(conn);
     }
   };
   ```

5. **ProactorPool Architecture**:
   ```cpp
   class ProactorPool {
     std::vector<std::unique_ptr<ProactorBase>> proactors_;
     
     // Round-robin proactor selection
     ProactorBase* GetNextProactor() {
       return proactors_[proactor_index_++ % proactors_.size()].get();
     }
     
     // Run all proactors
     void Run() {
       for (auto& p : proactors_) {
         threads_.emplace_back([&p] { p->Run(); });
       }
     }
   };
   ```

#### Key Design Patterns Observed

1. **Fiber-Centric I/O**:
   - All I/O operations are synchronous-looking but fiber-aware
   - `FiberCall` wrapper yields fiber until I/O completes
   - No explicit callbacks or async/await chains

2. **Per-Connection Fiber**:
   - Each connection runs in its own fiber
   - `HandleRequests()` is the main loop
   - Fiber is detached (fire-and-forget)

3. **Cross-Proactor Communication**:
   - `DispatchBrief()`: Fire-and-forget task submission
   - `Await()`: Wait for task completion
   - Uses `mpmc_bounded_queue` for task queue

4. **Intrusive Connection Management**:
   - Connections stored in intrusive lists
   - Thread-local connection lists per listener
   - Efficient iteration and cleanup

5. **Socket Abstraction**:
   - `FiberSocketBase` as common interface
   - Multiple implementations (UringSocket, EpollSocket)
   - Clean separation between socket and proactor

### Key Insights from DragonflyDB

Based on comprehensive source code analysis:

1. **Fibers for I/O Only**: DragonflyDB uses fibers primarily for network I/O operations
2. **No Global Executor**: Each I/O operation is scoped to its connection's fiber
3. **Thread-Local Context**: Each fiber runs in a specific Proactor thread
4. **Minimal State**: Fibers carry minimal state (socket, connection context)
5. **Clear Ownership**: Socket and connection lifetime are well-defined
6. **Synchronous-Looking Code**: Fiber abstraction makes async I/O look synchronous

### What DragonflyDB Does NOT Do

- ❌ No global `Executor` that all fibers post to
- ❌ No `HandleCommandAsync()` with awaitable execution
- ❌ No `HandleBatchCommandsAsync()` with awaitable batch processing
- ❌ No global connection pool shared across proactors
- ❌ No global buffer pool shared across proactors
- ❌ No C++20 coroutines (uses custom Fiber implementation)

### Why This Matters for AstraDB

AstraDB's NO SHARING architecture is **already superior** in many ways to DragonflyDB's approach:

- ✅ **Per-Worker Isolation**: Better than DragonflyDB's shared ProactorPool
- ✅ **SO_REUSEPORT**: Better kernel-level load balancing (Dragonfly uses round-robin)
- ✅ **Private Databases**: No cross-thread synchronization needed
- ✅ **MPSC Queues**: More efficient cross-worker communication than Dragonfly's Await/Dispatch
- ✅ **C++20 Coroutines**: Standard language feature vs custom Fiber implementation

**The goal is NOT to copy DragonflyDB's architecture, but to learn from their fiber usage patterns and apply them to C++20 coroutines.**

### DragonflyDB vs AstraDB Architecture Comparison

| Aspect | DragonflyDB | AstraDB (Current) | AstraDB (With Coroutines) |
|--------|-------------|-------------------|---------------------------|
| **Concurrency Model** | Custom Fibers | Callback-based async | C++20 Coroutines |
| **Thread Model** | ProactorPool (N threads) | Workers (N threads) | Workers (N threads) |
| **Load Balancing** | Round-robin per connection | SO_REUSEPORT (kernel) | SO_REUSEPORT (kernel) |
| **I/O Abstraction** | FiberSocketBase | asio::ip::tcp::socket | asio::ip::tcp::socket |
| **Connection Lifecycle** | Fiber per connection | Callback chain | Coroutine per connection |
| **Cross-Thread Communication** | Dispatch/Await | MPSC queues | MPSC queues |
| **Command Execution** | Synchronous in fiber | Synchronous in Executor | Synchronous in Executor |
| **Error Handling** | Exceptions | Error codes | Exceptions (coroutine-aware) |
| **Code Readability** | High (synchronous-looking) | Medium (callback chains) | High (synchronous-looking) |

---

## Asio Coroutine Fundamentals

### What is asio::awaitable?

`asio::awaitable<T>` is Asio's coroutine support for C++20:

```cpp
template<typename T>
class awaitable {
  // A coroutine that can be co_awaited
};
```

### Basic Usage Pattern

```cpp
asio::awaitable<void> async_operation(asio::ip::tcp::socket& socket) {
  // Read some data
  size_t bytes_read = co_await socket.async_read_some(
    asio::buffer(buffer),
    asio::use_awaitable
  );

  // Process data
  process_data(buffer, bytes_read);

  // Write response
  size_t bytes_written = co_await asio::async_write(
    socket,
    asio::buffer(response),
    asio::use_awaitable
  );
}
```

### Spawning Coroutines

Coroutines must be spawned with `co_spawn()`:

```cpp
asio::co_spawn(
  io_context,
  async_operation(socket),
  asio::detached
);
```

### Key Characteristics

1. **Sequential Code**: Coroutines make async code look synchronous
2. **Exception Safety**: Exceptions propagate through coroutine chain
3. **Cancellation**: Coroutines support cancellation via cancellation tokens
4. **No Thread Switching**: Coroutines execute on the io_context's thread

### Performance Characteristics

- **Zero-Overhead Abstraction**: Coroutines compile to state machines
- **No Heap Allocation**: Small coroutines use stack allocation
- **Excellent Cache Locality**: Better than callback chains
- **Easier to Read**: Sequential code is easier to reason about

### Dragonfly Fibers vs Asio Coroutines

| Aspect | Dragonfly Fibers | Asio Coroutines |
|--------|------------------|-----------------|
| **Implementation** | Custom fiber library | C++20 language feature |
| **Context Switch** | Manual (fiber scheduler) | Automatic (compiler) |
| **Stack Management** | Custom stack allocation | Compiler-managed |
| **Integration** | Tight with Proactor | Asio-specific |
| **Portability** | Linux-specific | Cross-platform |
| **Standard** | Proprietary | ISO C++20 |
| **Debugging** | Custom tooling | Standard debugger support |
| **Performance** | Optimized for Dragonfly | Generic but efficient |

### Code Pattern Comparison

#### Dragonfly Fiber Pattern:
```cpp
// Dragonfly: FiberSocketBase::Recv
Result<size_t> Recv(const io::MutableBytes& mb, int flags = 0) {
  FiberCall fc(proactor, timeout());
  fc->PrepRecv(fd, mb.data(), mb.size(), flags);
  res = fc.Get();  // Yields fiber until I/O completes
  if (res >= 0) {
    return res;
  }
  error_code ec(res, system_category());
  return make_unexpected(std::move(ec));
}

// Usage in Connection
void HandleRequests() {
  AsioStreamAdapter<FiberSocketBase> asa(*peer);
  while (true) {
    size_t res = asa.read_some(mb, ec);
    if (FiberSocketBase::IsConnClosed(ec))
      break;
    // Process data
  }
}
```

#### Asio Coroutine Pattern:
```cpp
// AstraDB: Connection::DoRead
asio::awaitable<void> DoRead() {
  while (true) {
    asio::error_code ec;
    size_t bytes = co_await socket_.async_read_some(
      asio::buffer(buffer_),
      asio::redirect_error(asio::use_awaitable, ec)
    );
    
    if (ec) {
      ASTRADB_LOG_ERROR("Read error: {}", ec.message());
      break;
    }
    
    receive_buffer_.append(buffer_.data(), bytes);
    ProcessCommands();
  }
}

// Usage: Spawn coroutine
asio::co_spawn(socket_.get_executor(), DoRead(), asio::detached);
```

#### Key Differences:

1. **Yielding Mechanism**:
   - Dragonfly: `fc.Get()` explicitly yields fiber
   - Asio: `co_await` implicitly yields coroutine

2. **Error Handling**:
   - Dragonfly: Returns `Result<T>` with error code
   - Asio: Uses `asio::error_code` with `redirect_error`

3. **Timeout Handling**:
   - Dragonfly: Timeout passed to `FiberCall` constructor
   - Asio: Parallel operations with `co_await (op1, op2)`

4. **Resource Management**:
   - Dragonfly: Manual socket lifetime management
   - Asio: RAII with `asio::ip::tcp::socket`

### Best Practices from Dragonfly Adapted for Asio Coroutines

1. **Per-Connection Coroutines** (Dragonfly pattern):
   ```cpp
   // Dragonfly: Fiber per connection
   next->DispatchBrief([this, conn] {
     fb2::Fiber(fb2::Launch::post, fb2::FixedStackAllocator(mr_, stack_size),
                "Connection", [this, conn] {
       RunSingleConnection(conn);
     }).Detach();
   });
   
   // AstraDB: Coroutine per connection
   asio::co_spawn(socket_.get_executor(), DoRead(), asio::detached);
   ```

2. **Timeout Management** (Dragonfly pattern):
   ```cpp
   // Dragonfly: Socket-level timeout
   socket->set_timeout(5000);  // 5 seconds
   Result<size_t> res = socket->Recv(mb, 0);
   
   // AstraDB: Operation-level timeout
   asio::steady_timer timer(socket_.get_executor());
   timer.expires_after(std::chrono::seconds(5));
   auto [ec, bytes, timer_ec] = co_await (read_op, timer_op);
   ```

3. **Error Propagation** (Dragonfly pattern):
   ```cpp
   // Dragonfly: Return Result<T>
   Result<size_t> Recv(...) {
     if (error) return make_unexpected(ec);
     return bytes;
   }
   
   // AstraDB: Use redirect_error
   asio::error_code ec;
   size_t bytes = co_await socket_.async_read_some(
     asio::buffer(buffer_),
     asio::redirect_error(asio::use_awaitable, ec)
   );
   if (ec) { /* handle error */ }
   ```

4. **Connection Lifecycle** (Dragonfly pattern):
   ```cpp
   // Dragonfly: Connection managed by ListenerInterface
   void RunSingleConnection(Connection* conn) {
     clist->Link(conn);
     try {
       conn->HandleRequests();
     } catch (std::exception& e) {
       LOG(ERROR) << "Exception: " << e.what();
     }
     OnConnectionClose(conn);
     socket_->Shutdown(SHUT_RDWR);
     socket_->Close();
     clist->Unlink(conn, this);
   }
   
   // AstraDB: Coroutine manages lifecycle
   asio::awaitable<void> DoRead() {
     try {
       while (true) {
         co_await Read();
         Process();
       }
     } catch (const std::exception& e) {
       ASTRADB_LOG_ERROR("Exception: {}", e.what());
     }
     Close();
   }
   ```

---

## Integration Strategy

### Core Philosophy

**Use coroutines to simplify I/O code, NOT to change the architecture.**

### What Will Use Coroutines

✅ **Connection I/O Operations**:
- `DoRead()` → `asio::awaitable<void> DoRead()`
- `Send()` → `asio::awaitable<void> Send()`
- Connection lifecycle management

✅ **Timeout Handling**:
- Connection timeout
- Read timeout
- Write timeout

### What Will NOT Use Coroutines

❌ **Command Execution**:
- `data_shard_.Execute()` remains synchronous
- Commands run in Executor thread
- No change to command processing

❌ **Cross-Worker Communication**:
- MPSC queues remain unchanged
- No async cross-worker operations

❌ **Global State**:
- No global Executor
- No global connection pool
- No global buffer pool

### Architecture After Integration

```
┌─────────────────────────────────────────────────────────────────┐
│                         Server                                   │
├─────────────────────────────────────────────────────────────────┤
│  Worker 0              Worker 1              Worker N           │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐     │
│  │ IO Thread    │     │ IO Thread    │     │ IO Thread    │     │
│  │ - Acceptor   │     │ - Acceptor   │     │ - Acceptor   │     │
│  │ - Conn Pool  │     │ - Conn Pool  │     │ - Conn Pool  │     │
│  │ - io_context │     │ - io_context │     │ - io_context │     │
│  │ - COROUTINES │     │ - COROUTINES │     │ - COROUTINES │     │
│  └──────┬───────┘     └──────┬───────┘     └──────┬───────┘     │
│         │                    │                    │              │
│         ▼                    ▼                    ▼              │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐     │
│  │ Executor     │     │ Executor     │     │ Executor     │     │
│  │ - Database   │     │ - Database   │     │ - Database   │     │
│  │ - Commands   │     │ - Commands   │     │ - Commands   │     │
│  │ - DataShard  │     │ - DataShard  │     │ - DataShard  │     │
│  │ - SYNC ONLY  │     │ - SYNC ONLY  │     │ - SYNC ONLY  │     │
│  └──────────────┘     └──────────────┘     └──────────────┘     │
└─────────────────────────────────────────────────────────────────┘
```

**Key Change**: Only the Connection class uses coroutines. Everything else remains the same.

---

## Implementation Plan

### Phase 1: Connection Refactoring (Week 1)

#### Task 1.1: Convert DoRead to Coroutine

**File**: `src/astra/server/worker.hpp` (Connection class)

**Current Implementation**:
```cpp
void DoRead() {
  auto self = shared_from_this();
  socket_.async_read_some(asio::buffer(buffer_),
    [this, self](asio::error_code ec, size_t bytes) {
      if (!ec) {
        receive_buffer_.append(buffer_.data(), bytes);
        ProcessCommands();
        DoRead();
      }
    });
}
```

**New Implementation** (inspired by Dragonfly's PingConnection):
```cpp
asio::awaitable<void> DoRead() {
  auto executor = co_await asio::this_coro::executor;
  
  // Set coroutine name for debugging (similar to Dragonfly's ThisFiber::SetName)
  std::string coro_name = absl::StrCat("Connection-", worker_id_, "-", conn_id_);
  
  while (true) {
    asio::error_code ec;
    size_t bytes = co_await socket_.async_read_some(
      asio::buffer(buffer_),
      asio::redirect_error(asio::use_awaitable, ec)
    );

    if (ec) {
      ASTRADB_LOG_ERROR("Worker {}: Connection {} read error: {}",
                       worker_id_, conn_id_, ec.message());
      break;
    }

    receive_buffer_.append(buffer_.data(), bytes);
    ProcessCommands();
    
    // Optional: Yield to allow other coroutines to run
    // (Dragonfiber's ThisFiber::Yield equivalent)
    // co_await asio::post(executor, asio::use_awaitable);
  }
  
  ASTRADB_LOG_DEBUG("Worker {}: Connection {} read loop terminated",
                   worker_id_, conn_id_);
}
```

**Benefits**:
- ✅ Cleaner, more readable code (Dragonfly pattern)
- ✅ Easier error handling with `redirect_error`
- ✅ Better exception safety (exceptions propagate naturally)
- ✅ Similar to Dragonfly's synchronous-looking I/O

**Dragonfly Comparison**:
```cpp
// Dragonfly (using FiberSocketBase)
size_t res = asa.read_some(mb, ec);
if (FiberSocketBase::IsConnClosed(ec))
  break;

// AstraDB (using coroutines)
size_t bytes = co_await socket_.async_read_some(
  asio::buffer(buffer_),
  asio::redirect_error(asio::use_awaitable, ec)
);
if (ec) break;
```

#### Task 1.2: Convert Send to Coroutine

**Current Implementation**:
```cpp
void Send(const std::string& response) {
  auto self = shared_from_this();
  std::string response_copy = response;
  asio::async_write(socket_, asio::buffer(response_copy),
    [this, self, response_copy](asio::error_code ec, size_t bytes) {
      if (!ec) {
        ASTRADB_LOG_DEBUG("Worker {}: Connection {} response sent (bytes={})",
                         worker_id_, conn_id_, bytes);
      } else {
        ASTRADB_LOG_ERROR("Worker {}: Connection {} write error: {}",
                          worker_id_, conn_id_, ec.message());
      }
    });
}
```

**New Implementation**:
```cpp
asio::awaitable<void> Send(const std::string& response) {
  asio::error_code ec;
  size_t bytes = co_await asio::async_write(
    socket_,
    asio::buffer(response),
    asio::redirect_error(asio::use_awaitable, ec)
  );

  if (!ec) {
    ASTRADB_LOG_DEBUG("Worker {}: Connection {} response sent (bytes={})",
                     worker_id_, conn_id_, bytes);
  } else {
    ASTRADB_LOG_ERROR("Worker {}: Connection {} write error: {}",
                      worker_id_, conn_id_, ec.message());
  }
}
```

**Benefits**:
- ✅ No need to capture `self` explicitly
- ✅ No need to copy response string
- ✅ Cleaner error handling
- ✅ Simpler than Dragonfly's FiberSocketBase::WriteSome

**Dragonfly Comparison**:
```cpp
// Dragonfly (using FiberSocketBase)
io::Result<size_t> WriteSome(const iovec* ptr, uint32_t len) {
  FiberCall fc(proactor, timeout());
  fc->PrepSend(fd, ptr->iov_base, ptr->iov_len, MSG_NOSIGNAL);
  res = fc.Get();  // Yields fiber until I/O completes
  return res;
}

// AstraDB (using coroutines)
asio::awaitable<void> Send(const std::string& response) {
  size_t bytes = co_await asio::async_write(
    socket_, asio::buffer(response),
    asio::redirect_error(asio::use_awaitable, ec)
  );
  // No explicit yield - co_await handles it
}
```

#### Task 1.3: Update Connection::Start()

**Current Implementation**:
```cpp
void Start() {
  ASTRADB_LOG_DEBUG("Worker {}: Connection {} starting", worker_id_, conn_id_);
  DoRead();
}
```

**New Implementation**:
```cpp
void Start() {
  ASTRADB_LOG_DEBUG("Worker {}: Connection {} starting", worker_id_, conn_id_);
  // Spawn coroutine (similar to Dragonfly's Fiber creation)
  asio::co_spawn(socket_.get_executor(), DoRead(), asio::detached);
}
```

**Benefits**:
- ✅ Explicit coroutine spawning
- ✅ Clear lifetime management
- ✅ Similar to Dragonfly's Fiber detach pattern

**Dragonfly Comparison**:
```cpp
// Dragonfly (using Fiber)
next->DispatchBrief([this, conn] {
  fb2::Fiber(fb2::Launch::post, fb2::FixedStackAllocator(mr_, stack_size),
             "Connection", [this, conn] {
    RunSingleConnection(conn);
  }).Detach();
});

// AstraDB (using coroutines)
asio::co_spawn(socket_.get_executor(), DoRead(), asio::detached);
```

#### Task 1.4: Update Response Sending

**Current Implementation** (in ProcessResponseQueue):
```cpp
if (resp_queue_.try_dequeue(resp)) {
  auto it = connections_.find(resp.conn_id);
  if (it != connections_.end()) {
    it->second->Send(resp.response);
  }
}
```

**New Implementation**:
```cpp
if (resp_queue_.try_dequeue(resp)) {
  auto it = connections_.find(resp.conn_id);
  if (it != connections_.end()) {
    // Spawn coroutine to send response (non-blocking)
    asio::co_spawn(
      it->second->GetSocket().get_executor(),
      [conn = it->second, response = resp.response]() -> asio::awaitable<void> {
        co_await conn->Send(response);
      },
      asio::detached
    );
  }
}
```

**Benefits**:
- ✅ Non-blocking response sending
- ✅ Better concurrency
- ✅ Similar to Dragonfly's fire-and-forget pattern

**Dragonfly Comparison**:
```cpp
// Dragonfly (using Fiber for responses)
// Responses sent synchronously in fiber (no async spawn needed)
asa.write_some(b, ec);

// AstraDB (using coroutines)
// Can spawn coroutine for non-blocking sends
asio::co_spawn(executor, Send(response), asio::detached);
```

### Phase 2: Timeout Handling (Week 2)

#### Task 2.1: Add Connection Timeout

**File**: `src/astra/server/worker.hpp` (Connection class)

**Implementation** (inspired by Dragonfly's FiberSocketBase timeout):
```cpp
class Connection : public std::enable_shared_from_this<Connection> {
public:
  Connection(size_t worker_id, uint64_t conn_id, asio::ip::tcp::socket socket,
             moodycamel::ConcurrentQueue<CommandWithConnId>* cmd_queue,
             moodycamel::ConcurrentQueue<ResponseWithConnId>* resp_queue)
      : worker_id_(worker_id),
        conn_id_(conn_id),
        socket_(std::move(socket)),
        cmd_queue_(cmd_queue),
        resp_queue_(resp_queue),
        timeout_timer_(socket_.get_executor()),
        idle_timeout_(std::chrono::seconds(300)) {  // 5 minutes default
    ASTRADB_LOG_DEBUG("Worker {}: Connection {} created", worker_id_, conn_id_);
  }

  asio::awaitable<void> DoRead() {
    auto executor = co_await asio::this_coro::executor;
    
    // Reset timeout timer (Dragonfly doesn't have this, but useful for AstraDB)
    timeout_timer_.expires_after(idle_timeout_);
    
    while (true) {
      // Wait for either read or timeout (using Asio's parallel completion)
      auto [read_ec, read_bytes, timer_ec] = co_await (
        socket_.async_read_some(asio::buffer(buffer_), 
                               asio::redirect_error(asio::use_awaitable, 
                                                  asio::error_code{})),
        timeout_timer_.async_wait(asio::redirect_error(asio::use_awaitable, 
                                                      asio::error_code{}))
      );
      
      // Check which operation completed first
      if (!timer_ec && read_ec == asio::error::operation_aborted) {
        // Timer fired first (timeout occurred)
        ASTRADB_LOG_WARN("Worker {}: Connection {} idle timeout", 
                        worker_id_, conn_id_);
        break;
      }
      
      if (!read_ec && timer_ec == asio::error::operation_aborted) {
        // Read completed, timer was cancelled
        if (read_bytes > 0) {
          receive_buffer_.append(buffer_.data(), read_bytes);
          ProcessCommands();
          // Reset timer for next iteration
          timeout_timer_.expires_after(idle_timeout_);
        }
        continue;
      }
      
      // Error occurred
      if (read_ec) {
        ASTRADB_LOG_ERROR("Worker {}: Connection {} read error: {}",
                         worker_id_, conn_id_, read_ec.message());
        break;
      }
    }
    
    ASTRADB_LOG_DEBUG("Worker {}: Connection {} closed due to timeout or error",
                     worker_id_, conn_id_);
    Close();
  }

  void SetIdleTimeout(std::chrono::seconds timeout) {
    idle_timeout_ = timeout;
  }

private:
  void Close() {
    asio::error_code ec;
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
    ASTRADB_LOG_DEBUG("Worker {}: Connection {} closed", worker_id_, conn_id_);
  }

  asio::steady_timer timeout_timer_;
  std::chrono::seconds idle_timeout_;
};
```

**Benefits**:
- ✅ Automatic cleanup of idle connections
- ✅ Resource leak prevention
- ✅ Similar to Dragonfly's `FiberSocketBase::set_timeout()` but for idle connections

**Dragonfly Comparison**:
```cpp
// Dragonfly (using FiberSocketBase)
class FiberSocketBase {
  virtual void set_timeout(uint32_t msec) = 0;
  virtual uint32_t timeout() const = 0;
};

// Usage in UringSocket
Result<size_t> Recv(const io::MutableBytes& mb, int flags = 0) override {
  FiberCall fc(proactor, timeout());  // Uses socket-level timeout
  fc->PrepRecv(fd, mb.data(), mb.size(), flags);
  return fc.Get();
}

// AstraDB (using coroutines)
// Idle timeout (no activity for N seconds)
asio::awaitable<void> DoRead() {
  timeout_timer_.expires_after(idle_timeout_);
  auto [read_ec, read_bytes, timer_ec] = co_await (read_op, timer_op);
}
```

#### Task 2.2: Add Read/Write Operation Timeout

**Implementation** (inspired by Dragonfly's FiberCall timeout):
```cpp
asio::awaitable<void> Send(const std::string& response) {
  asio::steady_timer write_timer(socket_.get_executor());
  write_timer.expires_after(std::chrono::seconds(5));  // 5 second write timeout

  auto [write_ec, write_bytes, timer_ec] = co_await (
    asio::async_write(socket_, asio::buffer(response),
                     asio::redirect_error(asio::use_awaitable, asio::error_code{})),
    write_timer.async_wait(asio::redirect_error(asio::use_awaitable, asio::error_code{}))
  );

  // Check which operation completed first
  if (!timer_ec && write_ec == asio::error::operation_aborted) {
    // Timer fired first (write timeout)
    ASTRADB_LOG_ERROR("Worker {}: Connection {} write timeout after 5s", 
                     worker_id_, conn_id_);
    Close();
    return;
  }
  
  if (!write_ec && timer_ec == asio::error::operation_aborted) {
    // Write completed successfully
    ASTRADB_LOG_DEBUG("Worker {}: Connection {} sent {} bytes",
                     worker_id_, conn_id_, write_bytes);
    return;
  }
  
  // Error occurred
  if (write_ec) {
    ASTRADB_LOG_ERROR("Worker {}: Connection {} write error: {}",
                     worker_id_, conn_id_, write_ec.message());
    Close();
  }
}

asio::awaitable<void> DoRead() {
  // ... existing code ...
  
  while (true) {
    // Read timeout (no data for N seconds during read operation)
    asio::steady_timer read_timer(socket_.get_executor());
    read_timer.expires_after(std::chrono::seconds(10));  // 10 second read timeout
    
    auto [read_ec, read_bytes, idle_timer_ec, read_timer_ec] = co_await (
      socket_.async_read_some(asio::buffer(buffer_),
                             asio::redirect_error(asio::use_awaitable, asio::error_code{})),
      timeout_timer_.async_wait(asio::redirect_error(asio::use_awaitable, asio::error_code{})),
      read_timer.async_wait(asio::redirect_error(asio::use_awaitable, asio::error_code{}))
    );
    
    // Handle various timeout scenarios
    // ...
  }
}
```

**Benefits**:
- ✅ Prevents slow I/O from blocking connection
- ✅ Similar to Dragonfly's `FiberCall` timeout mechanism
- ✅ Better resource management

**Dragonfly Comparison**:
```cpp
// Dragonfly (using FiberCall)
Result<size_t> Recv(const io::MutableBytes& mb, int flags = 0) override {
  FiberCall fc(proactor, timeout());  // Operation-level timeout
  fc->PrepRecv(fd, mb.data(), mb.size(), flags);
  res = fc.Get();  // Returns error if timeout
  if (res < 0) {
    ec = error_code(-res, system_category());
  }
  return make_unexpected(std::move(ec));
}

// AstraDB (using coroutines)
asio::awaitable<void> Send(const std::string& response) {
  asio::steady_timer write_timer(socket_.get_executor());
  write_timer.expires_after(std::chrono::seconds(5));
  auto [write_ec, write_bytes, timer_ec] = co_await (write_op, timer_op);
}
```

### Phase 3: Testing and Validation (Week 3)

#### Task 3.1: Unit Tests

Create unit tests for Connection coroutines:

**File**: `tests/unit/connection_test.cpp`

```cpp
TEST(ConnectionTest, CoroutineRead) {
  // Test coroutine-based read
}

TEST(ConnectionTest, CoroutineSend) {
  // Test coroutine-based send
}

TEST(ConnectionTest, TimeoutHandling) {
  // Test timeout handling
}
```

#### Task 3.2: Integration Tests

Test with redis-cli:

```bash
# Basic operations
redis-cli -p 6379 PING
redis-cli -p 6379 SET key value
redis-cli -p 6379 GET key

# Concurrent operations
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 100000 -c 50

# Timeout testing
# (test with slow client)
```

#### Task 3.3: Performance Testing

Compare performance before and after coroutine integration:

```bash
# Before: Baseline performance
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 100

# After: Coroutine performance
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 100
```

**Expected Result**: Performance should be similar or slightly better (due to better cache locality).

### Phase 4: Documentation and Cleanup (Week 4)

#### Task 4.1: Update Documentation

Update the following documents:
- `README.md`: Mention coroutine support
- `AstraDB_DESIGN.md`: Document coroutine usage
- `DOCS/io-uring-architecture-best-practices.md`: Update with coroutine patterns

#### Task 4.2: Code Cleanup

- Remove unused callback-based code
- Add comments explaining coroutine usage
- Ensure consistent coding style

#### Task 4.3: Create Examples

Create example code showing coroutine usage:

**File**: `examples/coroutines/connection_example.cpp`

```cpp
// Example of using Connection with coroutines
```

---

## Testing Strategy

### Unit Tests

#### Connection Tests
- ✅ Coroutine-based read operations
- ✅ Coroutine-based write operations
- ✅ Timeout handling
- ✅ Error handling
- ✅ Connection lifecycle

#### Worker Tests
- ✅ Multi-worker concurrent operations
- ✅ Command routing
- ✅ Response handling
- ✅ Cross-worker communication

### Integration Tests

#### Redis Compatibility Tests
```bash
# Test all basic Redis commands
redis-cli -p 6379 PING
redis-cli -p 6379 SET key value
redis-cli -p 6379 GET key
redis-cli -p 6379 MSET key1 value1 key2 value2
redis-cli -p 6379 MGET key1 key2
```

#### Performance Tests
```bash
# Benchmark SET operations
redis-benchmark -h 127.0.0.1 -p 6379 -t set -n 1000000 -c 100

# Benchmark GET operations
redis-benchmark -h 127.0.0.1 -p 6379 -t get -n 1000000 -c 100

# Benchmark mixed operations
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 100
```

#### Stress Tests
```bash
# High concurrency
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 10000000 -c 1000

# Large payloads
redis-cli -p 6379 SET bigkey $(python3 -c 'print("A" * 1024 * 1024)')

# Long-running connections
redis-cli -p 6379 --idle 600
```

### Regression Tests

Ensure no functionality is broken:

- ✅ All Redis commands still work
- ✅ AOF persistence still works
- ✅ RDB persistence still works
- ✅ Cluster commands still work (if implemented)
- ✅ ACL authentication still works (if implemented)

---

## Performance Considerations

### Expected Performance Impact

#### Positive Impacts
- **Better Cache Locality**: Coroutine state machines have better cache locality than callback chains
- **Fewer Heap Allocations**: Coroutines can use stack allocation for small state
- **Cleaner Code**: Easier to optimize and maintain

#### Neutral Impacts
- **Similar Throughput**: Coroutines should have similar or slightly better throughput
- **Similar Latency**: No significant latency changes expected

#### Potential Concerns
- **Coroutine Overhead**: Slight overhead for coroutine state management (typically negligible)
- **Stack Usage**: Coroutines may use more stack space (mitigated by small coroutine frames)

### Performance Monitoring

Monitor the following metrics:

```cpp
// Connection metrics
- Connection lifetime
- Read/write latency
- Timeout rate
- Error rate

// Worker metrics
- Queue depth
- Command execution time
- CPU utilization
- Memory usage
```

### Optimization Strategies

If performance degradation is observed:

1. **Reduce Coroutine State**: Minimize state captured in coroutines
2. **Use Stackless Coroutines**: Ensure compiler optimizes coroutine frames
3. **Batch Operations**: Batch multiple I/O operations in single coroutine
4. **Tune io_concurrency**: Adjust Asio's io_concurrency hint

---

## Risk Mitigation

### Technical Risks

#### Risk 1: Coroutine Performance Overhead
**Mitigation**:
- Profile coroutine performance before and after
- Use compiler optimizations (-O3, -flto)
- Minimize coroutine state size
- Consider using `[[clang::always_inline]]` for hot paths

#### Risk 2: Exception Handling Complexity
**Mitigation**:
- Add comprehensive exception handling
- Log all exceptions with stack traces
- Test error paths thoroughly
- Use `co_await` with `redirect_error` for better error handling

#### Risk 3: Coroutine Cancellation
**Mitigation**:
- Implement graceful cancellation
- Clean up resources on cancellation
- Test cancellation scenarios
- Document cancellation behavior

#### Risk 4: Compiler Compatibility
**Mitigation**:
- Test with multiple compilers (GCC, Clang)
- Verify C++20 coroutine support
- Use stable compiler versions (GCC 13+, Clang 16+)
- Document compiler requirements

### Implementation Risks

#### Risk 1: Breaking Existing Functionality
**Mitigation**:
- Comprehensive test suite
- Gradual migration (one component at a time)
- Keep old code as reference
- Continuous integration testing

#### Risk 2: Increased Complexity
**Mitigation**:
- Keep coroutines simple and focused
- Add clear documentation
- Use consistent patterns
- Code reviews for all changes

#### Risk 3: Debugging Difficulties
**Mitigation**:
- Add extensive logging
- Use coroutine-aware debuggers
- Document coroutine flow
- Create debugging guides

---

## Success Criteria

### Functional Requirements

✅ **All Tests Pass**:
- Unit tests: 100% pass rate
- Integration tests: 100% pass rate
- Redis compatibility tests: 100% pass rate

✅ **No Regressions**:
- All existing commands still work
- AOF persistence still works
- RDB persistence still works
- Performance not degraded

✅ **Code Quality**:
- Clean, readable coroutine code
- Comprehensive documentation
- Consistent coding style
- No compiler warnings

### Performance Requirements

✅ **Throughput**:
- SET QPS: ≥ 60,000 (baseline: 62,893)
- GET QPS: ≥ 60,000 (baseline: 62,150)
- No significant degradation (> 10%)

✅ **Latency**:
- P95 latency: ≤ 1.5x baseline
- P99 latency: ≤ 1.5x baseline
- Max latency: ≤ 2x baseline

✅ **Resource Usage**:
- Memory usage: ≤ 1.1x baseline
- CPU usage: ≤ 1.1x baseline
- No memory leaks

### Maintainability Requirements

✅ **Code Simplicity**:
- Connection class: ≤ 30% more code
- Easier to understand than callback version
- Clear coroutine flow

✅ **Documentation**:
- All coroutine usage documented
- Examples provided
- Best practices documented

---

## Appendix

### A. Code Examples

#### Example 1: Simple Coroutine

```cpp
asio::awaitable<void> simple_coroutine() {
  auto executor = co_await asio::this_coro::executor;
  
  // Do some async work
  co_await asio::steady_timer(executor).async_wait(
    asio::chrono::seconds(1),
    asio::use_awaitable
  );
  
  // More async work
}
```

#### Example 2: Coroutine with Error Handling

```cpp
asio::awaitable<void> coroutine_with_error_handling() {
  auto executor = co_await asio::this_coro::executor;
  
  asio::error_code ec;
  co_await some_async_operation(
    asio::redirect_error(asio::use_awaitable, ec)
  );
  
  if (ec) {
    // Handle error
    ASTRADB_LOG_ERROR("Operation failed: {}", ec.message());
  }
}
```

#### Example 3: Parallel Operations

```cpp
asio::awaitable<void> parallel_operations() {
  auto executor = co_await asio::this_coro::executor;
  
  // Run two operations in parallel
  auto [ec1, result1] = co_await (
    operation1(asio::use_awaitable),
    operation2(asio::use_awaitable)
  );
  
  // Process results
}
```

### B. Compiler Requirements

**Minimum Compiler Versions**:
- GCC: 13.0 or later
- Clang: 16.0 or later
- MSVC: 19.38 or later (Visual Studio 2022)

**C++ Standard**: C++20 or later

**Required Compiler Flags**:
```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

### C. Asio Version

**Minimum Version**: Asio 1.22.0 or later

**CPM Dependency**:
```cmake
CPMAddPackage(
  NAME asio
  VERSION 1.30.2
  GITHUB_REPOSITORY chriskohlhoff/asio
  OPTIONS
    ASIO_STANDALONE ON
    ASIO_NO_DEPRECATED ON
)
```

### D. References

- [Asio C++20 Coroutines Documentation](https://think-async.com/Asio/asio-1.30.2/doc/asio/overview/core/cpp20_coroutines.html)
- [C++20 Coroutines TS](https://en.cppreference.com/w/cpp/language/coroutines)
- [DragonflyDB Architecture](https://github.com/dragonflydb/dragonfly)
- [AstraDB NO SHARING Architecture](DOCS/io-uring-architecture-best-practices.md)

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-03-13 | AstraDB Team | Initial version |
| 1.1 | 2026-03-13 | AstraDB Team | Added DragonflyDB source code analysis and comparisons |

---

## Appendix E: Decision Matrix - Why Asio Coroutines for AstraDB?

### Technical Comparison

| Criteria | Dragonfly Fibers | Asio Coroutines | AstraDB Choice |
|----------|------------------|-----------------|----------------|
| **Standard Compliance** | Proprietary | ISO C++20 | ✅ Asio Coroutines |
| **Portability** | Linux-only | Cross-platform | ✅ Asio Coroutines |
| **Debugging Support** | Custom tools | Standard debuggers | ✅ Asio Coroutines |
| **Community Support** | Dragonfly-specific | Broad Asio community | ✅ Asio Coroutines |
| **Learning Curve** | Steep (custom API) | Moderate (C++20) | ✅ Asio Coroutines |
| **Performance** | Optimized for Dragonfly | Generic but efficient | ⚖️ Similar |
| **Integration with AstraDB** | Requires significant changes | Drop-in replacement | ✅ Asio Coroutines |
| **Compiler Support** | Requires specific compilers | GCC 13+, Clang 16+ | ✅ Asio Coroutines |
| **Future-Proofing** | Dependent on Dragonfly | Standard language feature | ✅ Asio Coroutines |

### Architectural Fit Analysis

#### Dragonfly Fibers in AstraDB:
```
❌ Would require:
   - Replacing Asio with custom Proactor implementation
   - Rewriting all socket abstractions
   - Implementing custom fiber scheduler
   - Losing standard library compatibility
   - Significant refactoring effort
   - Vendor lock-in to Dragonfly patterns

✅ Benefits:
   - Battle-tested in production
   - Optimized for high performance
   - Fine-grained control over scheduling
```

#### Asio Coroutines in AstraDB:
```
✅ Fits naturally:
   - Works with existing asio::ip::tcp::socket
   - No major architectural changes needed
   - Maintains NO SHARING principles
   - Standard C++20 feature
   - Easy to maintain and debug

✅ Benefits:
   - Clean, readable code
   - Standard language support
   - Broad community knowledge
   - Future-proof
   - Minimal learning curve
```

### Final Recommendation

**Use Asio Coroutines (C++20)** for AstraDB:

1. **Minimal Changes**: Only Connection class needs modification
2. **Standard Compliance**: Leverages C++20 standard features
3. **Maintainability**: Easier to understand and maintain
4. **Debuggability**: Standard debugger support
5. **Portability**: Works across platforms
6. **Performance**: Comparable to Dragonfly Fibers
7. **Future-Proof**: Based on language standard, not proprietary code

**Key Learning from Dragonfly**:
- ✅ Adopt per-connection coroutine pattern
- ✅ Use synchronous-looking async code
- ✅ Implement timeout handling
- ✅ Clean connection lifecycle management
- ❌ Do not adopt custom Fiber implementation
- ❌ Do not use Proactor architecture
- ❌ Do not use cross-proactor dispatch

**Architecture Decision**:
```
Keep:  NO SHARING architecture, SO_REUSEPORT, MPSC queues, Worker isolation
Add:   Asio coroutines for I/O layer only
Skip:  Dragonfly's custom Fiber, Proactor, cross-proactor dispatch
```

### Implementation Roadmap Summary

**Phase 1**: Convert Connection to coroutines (1 week)
**Phase 2**: Add timeout handling (1 week)
**Phase 3**: Testing and validation (1 week)
**Phase 4**: Documentation and cleanup (1 week)

**Total Effort**: 4 weeks
**Risk**: Low (incremental changes, backward compatible)
**Benefit**: Cleaner code, better maintainability, standard compliance

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-03-13 | AstraDB Team | Initial version |
| 1.1 | 2026-03-13 | AstraDB Team | Added DragonflyDB source code analysis and comparisons |
| 1.2 | 2026-03-13 | AstraDB Team | Added decision matrix and final recommendation |

---

**Document Version**: 1.0
**Created**: 2026-03-13
**Last Updated**: 2026-03-13
**Status**: Draft