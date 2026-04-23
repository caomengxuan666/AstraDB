#!/bin/bash
# AstraDB Coroutine Functionality Test Script
# Tests coroutine-based I/O implementation in Connection class

set -e

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test configuration
ASTRADB_HOST=${ASTRADB_HOST:-127.0.0.1}
ASTRADB_PORT=${ASTRADB_PORT:-6379}
TEST_DIR="test_results"
LOG_DIR="$TEST_DIR/logs"
BENCHMARK_DIR="$TEST_DIR/benchmarks"

# Create test directories
mkdir -p "$LOG_DIR"
mkdir -p "$BENCHMARK_DIR"

# Test counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Test function
test_command() {
    local description="$1"
    local command="$2"
    local expected="$3"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    echo -n "Test: $description ... "
    
    result=$(redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT $command 2>&1)
    
    if [ "$result" = "$expected" ]; then
        echo -e "${GREEN}✓ PASS${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ FAIL${NC}"
        echo "  Expected: $expected"
        echo "  Actual: $result"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
}

# Print section header
print_section() {
    echo ""
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}$1${NC}"
    echo -e "${YELLOW}========================================${NC}"
}

# Print subsection header
print_subsection() {
    echo ""
    echo -e "${BLUE}--- $1 ---${NC}"
}

# Check if server is running
check_server() {
    print_section "Checking Server Status"
    echo -n "Server status ... "
    if redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT PING > /dev/null 2>&1; then
        echo -e "${GREEN}Running${NC}"
        return 0
    else
        echo -e "${RED}Not Running${NC}"
        echo "Please start AstraDB server first!"
        exit 1
    fi
}

# Test 1: Basic Coroutine I/O Operations
test_basic_io() {
    print_section "Test 1: Basic Coroutine I/O Operations"
    
    # Test simple read/write
    test_command "PING" "PING" "PONG"
    test_command "SET key1 value1" "SET key1 value1" "OK"
    test_command "GET key1" "GET key1" "value1"
    test_command "DEL key1" "DEL key1" "1"
    
    # Test multiple commands in sequence
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT SET seq1 "a" > /dev/null
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT SET seq2 "b" > /dev/null
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT SET seq3 "c" > /dev/null
    test_command "GET seq1" "GET seq1" "a"
    test_command "GET seq2" "GET seq2" "b"
    test_command "GET seq3" "GET seq3" "c"
}

# Test 2: Concurrent Connections (Coroutine Spawn Test)
test_concurrent_connections() {
    print_section "Test 2: Concurrent Connections (Coroutine Spawn)"
    
    print_subsection "2.1: Multiple concurrent clients"
    
    # Launch 10 concurrent clients
    for i in {1..10}; do
        (
            redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT SET "conc_key_$i" "value_$i" > /dev/null 2>&1
            redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT GET "conc_key_$i" > /dev/null 2>&1
        ) &
    done
    
    # Wait for all background jobs
    wait
    
    # Verify all keys were set correctly
    all_passed=true
    for i in {1..10}; do
        result=$(redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT GET "conc_key_$i" 2>&1)
        if [ "$result" != "value_$i" ]; then
            echo -e "${RED}✗ FAIL: conc_key_$i = $result (expected value_$i)${NC}"
            all_passed=false
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi
    done
    
    if $all_passed; then
        echo -e "${GREEN}✓ PASS: All 10 concurrent operations succeeded${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    fi
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    # Cleanup
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT DEL conc_key_{1..10} > /dev/null 2>&1
}

# Test 3: Large Data Transfer
test_large_data() {
    print_section "Test 3: Large Data Transfer (Coroutine Buffer Handling)"
    
    print_subsection "3.1: Large string value"
    local large_value=$(python3 -c "print('x' * 10000)")
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT SET large_key "$large_value" > /dev/null 2>&1
    result=$(redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT STRLEN large_key 2>&1)
    
    if [ "$result" = "10000" ]; then
        echo -e "${GREEN}✓ PASS: Large string (10KB) transferred successfully${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ FAIL: Large string transfer failed (expected 10000, got $result)${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    print_subsection "3.2: Multiple large keys"
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT DEL large_key > /dev/null 2>&1
    for i in {1..50}; do
        redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT SET "bulk_$i" "$(python3 -c "print('x' * 1000)")" > /dev/null 2>&1
    done
    
    count=$(redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT DBSIZE 2>&1)
    if [ "$count" -ge 50 ]; then
        echo -e "${GREEN}✓ PASS: 50 bulk operations completed${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ FAIL: Bulk operations failed (expected 50+, got $count)${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    # Cleanup
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT FLUSHDB > /dev/null 2>&1
}

# Test 4: Error Handling and Edge Cases
test_error_handling() {
    print_section "Test 4: Error Handling (Coroutine Exception Safety)"
    
    print_subsection "4.1: Invalid command handling"
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    result=$(redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT INVALID_COMMAND 2>&1 | head -1)
    if echo "$result" | grep -q "ERR"; then
        echo -e "${GREEN}✓ PASS: Invalid command rejected${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ FAIL: Invalid command not rejected${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    print_subsection "4.2: Missing argument handling"
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    result=$(redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT GET 2>&1 | head -1)
    if echo "$result" | grep -q "ERR"; then
        echo -e "${GREEN}✓ PASS: Missing argument rejected${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ FAIL: Missing argument not rejected${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    print_subsection "4.3: Wrong type handling"
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT SET string_key "value" > /dev/null 2>&1
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    result=$(redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT LPUSH string_key "item" 2>&1 | head -1)
    if echo "$result" | grep -q "ERR"; then
        echo -e "${GREEN}✓ PASS: Wrong type operation rejected${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ FAIL: Wrong type operation not rejected${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    # Cleanup
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT FLUSHDB > /dev/null 2>&1
}

# Test 5: Connection Lifecycle
test_connection_lifecycle() {
    print_section "Test 5: Connection Lifecycle (Coroutine Spawn/Exit)"
    
    print_subsection "5.1: Connection acceptance and cleanup"
    
    # Open and close multiple connections rapidly
    for i in {1..20}; do
        redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT PING > /dev/null 2>&1
    done
    
    # Verify server still responsive
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT PING > /dev/null 2>&1; then
        echo -e "${GREEN}✓ PASS: Server still responsive after 20 rapid connections${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ FAIL: Server not responsive after rapid connections${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    print_subsection "5.2: Long-running connection"
    
    # Test that long connection stays alive
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT SET long_conn_test "alive" > /dev/null 2>&1
    sleep 2
    result=$(redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT GET long_conn_test 2>&1)
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [ "$result" = "alive" ]; then
        echo -e "${GREEN}✓ PASS: Long connection maintained state${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ FAIL: Long connection lost state${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    # Cleanup
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT FLUSHDB > /dev/null 2>&1
}

# Test 6: Performance Benchmark
test_performance() {
    print_section "Test 6: Performance Benchmark (Coroutine vs Callback)"
    
    local benchmark_file="$BENCHMARK_DIR/coroutine_perf_$(date +%Y%m%d_%H%M%S).txt"
    
    print_subsection "6.1: SET operations"
    echo "=== SET Operations ===" >> "$benchmark_file"
    redis-benchmark -h $ASTRADB_HOST -p $ASTRADB_PORT -t set -n 10000 -c 10 | tee -a "$benchmark_file"
    
    print_subsection "6.2: GET operations"
    echo "=== GET Operations ===" >> "$benchmark_file"
    redis-benchmark -h $ASTRADB_HOST -p $ASTRADB_PORT -t get -n 10000 -c 10 | tee -a "$benchmark_file"
    
    print_subsection "6.3: Mixed operations"
    echo "=== Mixed Operations ===" >> "$benchmark_file"
    redis-benchmark -h $ASTRADB_HOST -p $ASTRADB_PORT -t set,get -n 10000 -c 10 | tee -a "$benchmark_file"
    
    echo -e "${GREEN}✓ Benchmark results saved to: $benchmark_file${NC}"
    
    # Cleanup
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT FLUSHDB > /dev/null 2>&1
}

# Test 7: Stress Test
test_stress() {
    print_section "Test 7: Stress Test (High Concurrency)"
    
    local stress_log="$LOG_DIR/stress_test_$(date +%Y%m%d_%H%M%S).log"
    
    print_subsection "7.1: High concurrency (50 clients)"
    
    # Launch 50 clients with 100 operations each
    for i in {1..50}; do
        (
            for j in {1..100}; do
                redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT SET "stress_${i}_${j}" "value" > /dev/null 2>&1
                redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT GET "stress_${i}_${j}" > /dev/null 2>&1
            done
        ) &
    done
    
    echo "Running stress test (5000 operations)..."
    wait
    
    # Verify server still responsive
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT PING > /dev/null 2>&1; then
        echo -e "${GREEN}✓ PASS: Server survived stress test${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ FAIL: Server crashed during stress test${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    # Check database size
    count=$(redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT DBSIZE 2>&1)
    echo "  Database size after stress test: $count keys"
    
    # Cleanup
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT FLUSHDB > /dev/null 2>&1
}

# Test 8: Cross-Worker Distribution
test_cross_worker() {
    print_section "Test 8: Cross-Worker Distribution (NO SHARING)"
    
    print_subsection "8.1: Key distribution across workers"
    
    # Set 100 keys that should be distributed across workers
    for i in {1..100}; do
        redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT SET "dist_key_$i" "value_$i" > /dev/null 2>&1
    done
    
    count=$(redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT DBSIZE 2>&1)
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [ "$count" = "100" ]; then
        echo -e "${GREEN}✓ PASS: All 100 keys stored correctly${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ FAIL: Expected 100 keys, got $count${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    print_subsection "8.2: Read from distributed keys"
    
    # Verify a sample of keys
    all_passed=true
    for i in {1..10}; do
        result=$(redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT GET "dist_key_$i" 2>&1)
        if [ "$result" != "value_$i" ]; then
            echo -e "${RED}✗ FAIL: dist_key_$i = $result (expected value_$i)${NC}"
            all_passed=false
        fi
    done
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if $all_passed; then
        echo -e "${GREEN}✓ PASS: All sampled keys retrieved correctly${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ FAIL: Some keys retrieved incorrectly${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    # Cleanup
    redis-cli -h $ASTRADB_HOST -p $ASTRADB_PORT FLUSHDB > /dev/null 2>&1
}

# Print summary
print_summary() {
    echo ""
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}Test Summary${NC}"
    echo -e "${YELLOW}========================================${NC}"
    echo "Total Tests:  $TOTAL_TESTS"
    echo -e "${GREEN}Passed:       $PASSED_TESTS${NC}"
    echo -e "${RED}Failed:       $FAILED_TESTS${NC}"
    
    if [ $FAILED_TESTS -eq 0 ]; then
        echo ""
        echo -e "${GREEN}✓ All tests passed!${NC}"
        return 0
    else
        echo ""
        echo -e "${RED}✗ Some tests failed!${NC}"
        return 1
    fi
}

# Main test execution
main() {
    echo "=========================================="
    echo "  AstraDB Coroutine Functionality Tests"
    echo "=========================================="
    echo ""
    echo "Test Configuration:"
    echo "  Host: $ASTRADB_HOST"
    echo "  Port: $ASTRADB_PORT"
    echo "  Test Dir: $TEST_DIR"
    echo ""
    
    # Check server status
    check_server
    
    # Run all tests
    test_basic_io
    test_concurrent_connections
    test_large_data
    test_error_handling
    test_connection_lifecycle
    test_performance
    test_stress
    test_cross_worker
    
    # Print summary
    print_summary
}

# Run main function
main "$@"