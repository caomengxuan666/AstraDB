#!/bin/bash
# Test script for MOVED/ASK redirection in cluster mode

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "=== Testing Cluster MOVED Redirection ==="

# Kill any existing astradb process
pkill -9 astradb || true
sleep 1

# Start astradb with cluster enabled
echo "Starting AstraDB in cluster mode..."
cd "$PROJECT_ROOT"

if [ -x "build-linux-release-debuginfo-clang/bin/astradb" ]; then
    ASTRADB_BIN="build-linux-release-debuginfo-clang/bin/astradb"
elif [ -x "build-linux-release-debuginfo-noasan/bin/astradb" ]; then
    ASTRADB_BIN="build-linux-release-debuginfo-noasan/bin/astradb"
elif [ -x "build/bin/astradb" ]; then
    ASTRADB_BIN="build/bin/astradb"
else
    echo "ERROR: astradb binary not found in known build directories."
    exit 1
fi

"./$ASTRADB_BIN" --config "$PROJECT_ROOT/astradb-cluster-test.toml" &
ASTRADB_PID=$!
sleep 2

echo "AstraDB started with PID: $ASTRADB_PID"

# Test 1: Normal GET (should work without cluster enabled)
echo ""
echo "Test 1: Normal GET command (cluster disabled by default)"
redis-cli -p 6379 GET mykey || echo "GET failed as expected (key not set)"

# Test 2: Set a key and retrieve it
echo ""
echo "Test 2: SET and GET a key"
redis-cli -p 6379 SET mykey "hello world"
redis-cli -p 6379 GET mykey

# Test 3: Check CLUSTER INFO
echo ""
echo "Test 3: Check CLUSTER INFO"
redis-cli -p 6379 CLUSTER INFO

# Test 4: Check CLUSTER NODES
echo ""
echo "Test 4: Check CLUSTER NODES"
redis-cli -p 6379 CLUSTER NODES

# Test 5: Check CLUSTER KEYSLOT
echo ""
echo "Test 5: Check CLUSTER KEYSLOT for 'mykey'"
redis-cli -p 6379 CLUSTER KEYSLOT mykey

# Test 6: Check CLUSTER SLOTS
echo ""
echo "Test 6: Check CLUSTER SLOTS"
redis-cli -p 6379 CLUSTER SLOTS

# Cleanup
echo ""
echo "Cleaning up..."
kill $ASTRADB_PID
sleep 1

echo "=== All tests completed ==="
