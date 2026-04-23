#!/bin/bash

# Performance Test Script for AstraDB
# Usage: sudo ./scripts/tests/perf_test.sh

set -e

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

cd "$PROJECT_ROOT"

echo "====================================="
echo "AstraDB Performance Test Script"
echo "====================================="
echo "Date: $(date)"
echo ""

# Check if built
if [ ! -f "build-linux-release-debuginfo-clang/bin/astradb" ]; then
    echo "Error: Astradb binary not found. Please build the project first."
    echo "Run: cmake --build build-linux-release-debuginfo-clang"
    exit 1
fi

# Clean up old processes
echo "Step 1: Cleaning up old processes..."
pkill -9 astradb redis-benchmark perf 2>/dev/null || true
sleep 2
echo "✓ Cleanup complete"
echo ""

# Create output directory
mkdir -p perf_results
echo "Step 2: Creating output directory..."
echo "✓ Output directory: perf_results"
echo ""

# Start server
echo "Step 3: Starting AstraDB server..."
./build-linux-release-debuginfo-clang/bin/astradb -l error --config astradb-dev.toml > perf_results/server.log 2>&1 &
SERVER_PID=$!
sleep 3

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Error: Server failed to start. Check perf_results/server.log for details."
    tail -50 perf_results/server.log
    exit 1
fi
echo "✓ Server started (PID: $SERVER_PID)"
echo ""

# Test connection
echo "Step 4: Testing server connection..."
if ! command -v redis-cli &> /dev/null; then
    echo "Warning: redis-cli not found. Skipping connection test."
else
    if redis-cli -h 127.0.0.1 -p 6379 ping > /dev/null 2>&1; then
        echo "✓ Server connection successful"
    else
        echo "Error: Server connection failed"
        tail -50 perf_results/server.log
        kill -9 $SERVER_PID 2>/dev/null
        exit 1
    fi
fi
echo ""

# Prepare perf output
PERF_OUTPUT="perf_results/perf_$(date +%Y%m%d_%H%M%S).data"
BENCHMARK_OUTPUT="perf_results/benchmark_$(date +%Y%m%d_%H%M%S).txt"

echo "Step 5: Starting performance recording for AstraDB (PID: $SERVER_PID)..."
echo "  Perf output: $PERF_OUTPUT"
echo "  Benchmark output: $BENCHMARK_OUTPUT"
echo ""

# Start perf recording for astradb process
sudo perf record -o "$PERF_OUTPUT" -g -p $SERVER_PID &
PERF_PID=$!
sleep 1

echo "  Command: redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 1"
echo ""

# Run benchmark (perf is recording astradb in background)
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 1 \
    > "$BENCHMARK_OUTPUT" 2>&1

BENCHMARK_EXIT_CODE=$?

# Stop perf recording
echo ""
echo "Stopping perf recording..."
sudo kill -INT $PERF_PID 2>/dev/null || true
wait $PERF_PID 2>/dev/null || true

echo ""
echo "====================================="
echo "Test Complete"
echo "====================================="
echo ""

# Print benchmark summary
if [ -f "$BENCHMARK_OUTPUT" ]; then
    echo "Benchmark Summary:"
    echo "-------------------"
    grep -A 5 "Summary:" "$BENCHMARK_OUTPUT" || true
    echo ""
fi

# Show perf data info
echo "Perf Data:"
echo "----------"
if [ -f "$PERF_OUTPUT" ]; then
    echo "✓ Perf data saved: $PERF_OUTPUT"
    echo "  Size: $(du -h "$PERF_OUTPUT" | cut -f1)"
    echo ""
    echo "To analyze perf data:"
    echo "  sudo perf report -i $PERF_OUTPUT"
    echo "  sudo perf annotate -i $PERF_OUTPUT"
else
    echo "✗ Perf data not found"
fi
echo ""

# Cleanup
echo "Step 6: Cleaning up..."
kill -9 $SERVER_PID 2>/dev/null || true
pkill -9 redis-benchmark 2>/dev/null || true
echo "✓ Cleanup complete"
echo ""

if [ $BENCHMARK_EXIT_CODE -eq 0 ]; then
    echo "✓ Test completed successfully"
    exit 0
else
    echo "✗ Test failed with exit code: $BENCHMARK_EXIT_CODE"
    exit 1
fi
