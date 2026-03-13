# io_uring Architecture Best Practices

## Core Architecture Pattern: Per-Worker IO Context

**Each worker = one thread + one io_context + one acceptor**

```
Worker 0: Thread 0 + io_context_0 + acceptor_0 (SO_REUSEPORT)
Worker 1: Thread 1 + io_context_1 + acceptor_1 (SO_REUSEPORT)
...
Worker N: Thread N + io_context_N + acceptor_N (SO_REUSEPORT)
```

## Why No Strand Needed?

**strand is designed to solve "single io_context + multiple threads" concurrency issues**

But our architecture is:
- **Each io_context has only one thread**
- **Socket always belongs to one io_context**
- **All operations execute on the same thread**
- **No strand needed!**

## Fatal Problems with Old Architecture

1. **Mixed three IO models**:
   - `IOContextThreadPool` (multiple io_contexts + multiple threads)
   - `Executor` (single io_context + multiple threads)
   - Main `io_context` (global accept)

2. **Global post mechanism**:
   ```cpp
   g_post_to_main_io_context_func
   ```
   Breaks architectural boundaries

3. **ConnectionPool object reuse**:
   ```cpp
   auto conn = connection_pool_->Acquire(socket);
   ```
   May cause socket to be passed between different io_contexts

4. **Executor participates in IO operations**:
   IO thread and CPU thread boundaries are blurred

## New Architecture (Pure Per-Worker)

```
IO Workers (N threads):
  - Each worker has independent io_context
  - Each worker has independent acceptor (SO_REUSEPORT)
  - Socket belongs to this worker from accept
  - IO callbacks do minimal parsing only
  - Business logic posted to Executor

CPU Workers (M threads):
  - Executor purely handles CPU-intensive tasks
  - Does not participate in IO operations
```

## Key Design Principles

### 1. Socket Single Ownership
- Socket from accept immediately creates Connection
- Connection belongs to acceptor's io_context
- No passing between io_contexts

### 2. IO Threads Only Do IO
```cpp
void DoRead() {
  socket.async_read_some(buffer, [this](ec, bytes) {
    if (ec) return;
    // Minimal parsing
    parser.Parse(buffer);
    // Post to executor
    executor.post([this] { ProcessCommands(); });
    DoRead();
  });
}
```

### 3. Remove All Mixing
- ❌ Delete `IOContextThreadPool` (old global pool)
- ❌ Delete main `io_context` (global accept)
- ❌ Delete global post mechanism
- ❌ Delete `ConnectionPool` (object reuse)
- ✅ Only `NewIOContextPool` (per-worker)
- ✅ Only `Executor` (CPU tasks)

### 4. Simplify Connection
```cpp
class Connection {
  Socket socket_;  // Directly from accept
  io_context& ctx_;  // Belonging io_context
  // No strand needed!
  // No pool reuse!
};
```

## Why Old Architecture Can't Use io_uring?

**io_uring problems when mixing multiple io_contexts**:

1. **Multiple io_contexts share io_uring instance** (from logs, only one fd=4 seen)
2. **Socket passing between io_contexts** causes completion event confusion
3. **Global post mechanism** breaks io_uring completion queue semantics

**Per-Worker mode solves all problems**:
- Each io_context is independent
- Socket not passed
- No post to other io_context

## Implementation Steps

1. **Delete all old code**:
   - Delete `IOContextThreadPool`
   - Delete main `io_context`
   - Delete `ConnectionPool`
   - Delete global post mechanism

2. **Keep only two components**:
   - `NewIOContextPool` (per-worker IO)
   - `Executor` (CPU tasks)

3. **Refactor Server**:
   - Only manages NewIOContextPool and Executor
   - Does not directly manage io_context
   - Does not directly manage Connection

4. **Refactor Connection**:
   - Created directly from accept
   - No pool reuse
   - No strand needed

## Testing Strategy

1. Verify examples work with io_uring (already done)
2. Implement new architecture in main server
3. Test with io_uring enabled
4. Compare with old architecture behavior

## References

- Original investigation: `DOCS/io-uring-investigation.md`
- Working examples: `examples/io_uring/`
- Copilot analysis: see conversation history