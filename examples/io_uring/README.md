# io_uring Debug Examples

This directory contains minimal test servers to investigate io_uring issues in AstraDB.

## Purpose

When AstraDB is configured to use io_uring instead of epoll on Linux, it fails to receive data. These examples help identify the root cause by comparing:

1. **Raw io_uring server** - Uses liburing API directly (baseline)
2. **ASIO io_uring server** - Uses ASIO with io_uring backend (same as main server)
3. **Test client** - Simple client to send/receive test messages

## Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install liburing-dev cmake build-essential

# Or check if io_uring is available
cat /proc/sys/kernel/io_uring_enabled  # Should be 1 or 2
```

## Building

```bash
# From the AstraDB root directory
mkdir -p build/io_uring_examples
cd build/io_uring_examples

cmake ../examples/io_uring
cmake --build .
```

## Running Tests

### Manual Testing

**Terminal 1 - Start raw io_uring server:**
```bash
./raw_io_uring_server 8765
```

**Terminal 2 - Start ASIO io_uring server:**
```bash
./asio_io_uring_server 8766
```

**Terminal 3 - Test clients:**
```bash
# Test raw io_uring server
./test_client -p 8765

# Test ASIO io_uring server
./test_client -p 8766

# Multiple tests
./test_client -p 8765 -n 10 -d 100

# Multiple connections
./test_client -p 8765 -m 20
```

### Automated Testing

```bash
# Test raw io_uring server
make test_raw_io_uring

# Test ASIO io_uring server
make test_asio_io_uring

# Test all servers
make test_all_io_uring
```

## Expected Behavior

Both servers should:
- Accept connections
- Receive "PING\\r\\n" messages
- Respond with "+OK\\r\\n"
- Handle multiple clients
- Handle multiple messages per connection

## Test Results

✅ **All io_uring examples compile and work correctly:**

1. **asio_io_uring_server.cpp** - Basic ASIO io_uring server
   - ✅ Accepts connections
   - ✅ Receives data correctly
   - ✅ Sends responses
   - ✅ Handles multiple clients

2. **asio_io_uring_strand_server.cpp** - ASIO io_uring with strand serialization
   - ✅ Accepts connections
   - ✅ Receives data correctly
   - ✅ Thread-safe with strand
   - ✅ Handles concurrent clients

3. **asio_io_uring_pool_server.cpp** - ASIO io_uring with io_context pool
   - ✅ Accepts connections
   - ✅ Receives data correctly
   - ✅ Multi-threaded with pool
   - ✅ High concurrency support

4. **asio_io_uring_pool_server_debug.cpp** - Extended architecture test
   - ✅ 12 pool threads + executor + main io_context
   - ✅ Accepts connections
   - ✅ Receives data correctly
   - ✅ Full architecture validation

5. **test_client.cpp** - Fixed test client
   - ✅ Sends correct 6-byte "PING\\r\\n" (not 7 bytes)
   - ✅ Receives responses correctly
   - ✅ No buffer corruption

**Conclusion:** All minimal io_uring examples work correctly. The issue is isolated to the main AstraDB server, not the io_uring backend itself.

## Main Server Issue

While all examples work correctly with io_uring, the main AstraDB server has issues:

❌ **Main Server (io_uring):**
- ✅ Accepts connections successfully
- ✅ Connection callbacks invoked
- ❌ `async_read_some` returns 0 bytes immediately
- ❌ No data received from clients
- ❌ Total commands processed: 0

**Comparison:**
- All examples: ✅ Work with io_uring
- Main server (epoll): ✅ Works
- Main server (io_uring): ❌ Fails to receive data

This suggests the issue is in the main server's specific initialization or configuration, not in io_uring itself.

## Debugging

If ASIO io_uring server fails to receive data:

1. **Check io_uring backend:**
   ```bash
   cat /proc/sys/kernel/io_uring_enabled
   ```

2. **Check ASIO backend type:**
   The ASIO server should print "Backend: io_uring" when started.

3. **Compare with raw io_uring:**
   - If raw io_uring works but ASIO doesn't: ASIO backend issue
   - If neither works: System/kernel issue

4. **Enable verbose logging:**
   ```bash
   strace -e trace=io_uring_setup,io_uring_enter,io_uring_submit ./asio_io_uring_server
   ```

5. **Check kernel version:**
   ```bash
   uname -r  # Should be 5.1+ for io_uring
   ```

## Key Differences

### Raw io_uring Server
- Uses `liburing` API directly
- Manual submission queue (SQE) and completion queue (CQE) management
- Full control over io_uring operations

### ASIO io_uring Server
- Uses ASIO abstraction layer
- Forces io_uring with `ASIO_HAS_IO_URING=1` and `ASIO_DISABLE_EPOLL=1`
- Same model as main AstraDB server
- Should behave identically to raw version if backend is working

## Common Issues

### "io_uring not available"
- Check kernel version: `uname -r` (must be 5.1+)
- Check io_uring support: `cat /proc/sys/kernel/io_uring_enabled`
- Try running as root

### "Cannot receive data"
- Verify backend is actually using io_uring (check server output)
- Test with raw io_uring server first
- Check firewall settings
- Try different port numbers

### ASIO using epoll instead of io_uring
- Verify `ASIO_HAS_IO_URING=1` and `ASIO_DISABLE_EPOLL=1` are defined
- Check CMakeLists.txt compile definitions
- Rebuild with clean: `rm -rf * && cmake .. && make`

## Next Steps

Once the issue is identified:

1. If raw io_uring works but ASIO doesn't: ASIO backend bug
2. If neither works: System/kernel configuration issue
3. Update main AstraDB server based on findings
4. Consider adding runtime io_uring detection and fallback

## Files

- `raw_io_uring_server.cpp` - Raw liburing implementation
- `asio_io_uring_server.cpp` - Basic ASIO with io_uring backend ✅
- `asio_io_uring_strand_server.cpp` - ASIO io_uring with strand ✅
- `asio_io_uring_pool_server.cpp` - ASIO io_uring with io_context pool ✅
- `asio_io_uring_pool_server_debug.cpp` - Extended architecture test ✅
- `test_client.cpp` - Test client (fixed to send 6 bytes) ✅
- `CMakeLists.txt` - Build configuration
- `README.md` - This file