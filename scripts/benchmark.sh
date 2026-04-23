#!/bin/bash
# Benchmark script for AstraDB vs Redis

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== Benchmark Script ==="
echo ""

# Kill existing servers
pkill -f "astradb" 2>/dev/null
pkill -f "redis-server" 2>/dev/null
sleep 1

# Start Redis on 6380
echo "Starting Redis on 6380..."
redis-server --port 6380 --daemonize yes --save "" --appendonly no
sleep 2

# Start AstraDB on 6379
echo "Starting AstraDB on 6379..."
cd "$PROJECT_ROOT"
./build-linux-package-clang/bin/astradb --config config/astradb-benchmark.toml &
sleep 3

echo ""
echo "=== Testing AstraDB (6379) ==="
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 100000 -c 50 -q

echo ""
echo "=== Testing Redis (6380) ==="
redis-benchmark -h 127.0.0.1 -p 6380 -t set,get -n 100000 -c 50 -q

echo ""
echo "=== Pipeline Test AstraDB ==="
redis-benchmark -h 127.0.0.1 -p 6379 -t set -n 100000 -c 50 -P 16 -q

echo ""
echo "=== Pipeline Test Redis ==="
redis-benchmark -h 127.0.0.1 -p 6380 -t set -n 100000 -c 50 -P 16 -q

echo ""
echo "Done!"
