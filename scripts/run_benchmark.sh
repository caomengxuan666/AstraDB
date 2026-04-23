#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "Starting benchmark..."

# Kill any existing processes
pkill -f "astradb" 2>/dev/null
pkill -f "redis-server" 2>/dev/null
sleep 2

# Start AstraDB with noasan build
echo "Starting AstraDB (noasan build)..."
cd "$PROJECT_ROOT"
./build-linux-release-debuginfo-noasan/bin/astradb --config config/astradb-benchmark.toml &
ASTRADB_PID=$!

# Wait for server to start
sleep 5

# Check if server started
if ! ps -p $ASTRADB_PID > /dev/null; then
    echo "ERROR: AstraDB failed to start!"
    exit 1
fi

echo "AstraDB started with PID: $ASTRADB_PID"

# Test basic functionality
echo "Testing basic SET/GET..."
redis-cli SET test_key "test_value"
redis-cli GET test_key

echo ""
echo "=== Benchmark Results ==="

# Run benchmarks
echo "AstraDB SET/GET (50 connections):"
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 100000 -c 50 -q

echo ""
echo "AstraDB Pipeline SET (50 connections, P=16):"
redis-benchmark -h 127.0.0.1 -p 6379 -t set -n 100000 -c 50 -P 16 -q

# Cleanup
echo ""
echo "Stopping AstraDB..."
kill $ASTRADB_PID
wait $ASTRADB_PID 2>/dev/null
echo "Benchmark completed."
