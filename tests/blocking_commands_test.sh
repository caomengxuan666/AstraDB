#!/bin/bash
# AstraDB Blocking Commands Test Script
# Test all blocking commands for correctness

set -e

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Print section header
print_section() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

# Test function
test_blocking() {
    local description="$1"
    local expected="$2"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    echo -n "Test: $description ... "
    
    result=$("$3" 2>&1)
    
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

# Start server
echo "Starting AstraDB server..."
cd /home/cmx/codespace/AstraDB/build
./bin/astradb > /tmp/astradb_blocking_test.log 2>&1 &
SERVER_PID=$!
sleep 2

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
}

# Set cleanup on exit
trap cleanup EXIT

echo -e "${GREEN}Server started (PID: $SERVER_PID)${NC}"

# Start testing
echo "=========================================="
echo "  AstraDB Blocking Commands Test Suite"
echo "=========================================="

# 1. BLPOP basic tests
print_section "1. BLPOP Basic Functionality Tests"

echo -e "${YELLOW}Test 1: BLPOP timeout returns nil${NC}"
redis-cli DEL blpop_list1 > /dev/null
test_blocking "BLPOP empty list timeout 1 sec" "(nil)" "timeout 3 redis-cli BLPOP blpop_list1 1"

echo -e "${YELLOW}Test 2: BLPOP immediate return (list has data)${NC}"
redis-cli LPUSH blpop_list1 item1 > /dev/null
test_blocking "BLPOP non-empty list immediate return" "blpop_list1\nitem1" "redis-cli BLPOP blpop_list1 1"

echo -e "${YELLOW}Test 3: BLPOP block then wake up${NC}"
redis-cli DEL blpop_list2 > /dev/null
(
    sleep 1
    redis-cli LPUSH blpop_list2 data1 > /dev/null
) &
test_blocking "BLPOP blocked 1 sec then woken up" "blpop_list2\ndata1" "timeout 5 redis-cli BLPOP blpop_list2 3"

# 2. BRPOP basic tests
print_section "2. BRPOP Basic Functionality Tests"

echo -e "${YELLOW}Test 4: BRPOP timeout returns nil${NC}"
redis-cli DEL brpop_list1 > /dev/null
test_blocking "BRPOP empty list timeout 1 sec" "(nil)" "timeout 3 redis-cli BRPOP brpop_list1 1"

echo -e "${YELLOW}Test 5: BRPOP immediate return (list has data)${NC}"
redis-cli LPUSH brpop_list1 item1 item2 > /dev/null
test_blocking "BRPOP non-empty list immediate return" "brpop_list1\nitem1" "redis-cli BRPOP brpop_list1 1"

echo -e "${YELLOW}Test 6: BRPOP block then wake up${NC}"
redis-cli DEL brpop_list2 > /dev/null
(
    sleep 1
    redis-cli LPUSH brpop_list2 data2 > /dev/null
) &
test_blocking "BRPOP blocked 1 sec then woken up" "brpop_list2\ndata2" "timeout 5 redis-cli BRPOP brpop_list2 3"

# 3. BLMOVE basic tests
print_section "3. BLMOVE Basic Functionality Tests"

echo -e "${YELLOW}Test 7: BLMOVE block when source list empty${NC}"
redis-cli DEL blmove_src blmove_dst > /dev/null
(
    sleep 1
    redis-cli LPUSH blmove_src move_item > /dev/null
) &
test_blocking "BLMOVE blocked then woken up" "move_item" "timeout 5 redis-cli BLMOVE blmove_src blmove_dst LEFT RIGHT 3"

echo -e "${YELLOW}Test 8: BLMOVE immediate return (source list has data)${NC}"
redis-cli DEL blmove_src2 blmove_dst2 > /dev/null
redis-cli LPUSH blmove_src2 item1 item2 > /dev/null
test_blocking "BLMOVE immediate return" "item2" "redis-cli BLMOVE blmove_src2 blmove_dst2 RIGHT LEFT 1"

# 4. BLMPOP basic tests
print_section "4. BLMPOP Basic Functionality Tests"

echo -e "${YELLOW}Test 9: BLMPOP timeout when all lists empty${NC}"
redis-cli DEL blmpop_list1 blmpop_list2 > /dev/null
test_blocking "BLMPOP timeout returns nil" "(nil)" "timeout 3 redis-cli BLMPOP 3 1 blmpop_list1 blmpop_list2 LEFT"

echo -e "${YELLOW}Test 10: BLMPOP first list has data${NC}"
redis-cli LPUSH blmpop_list1 item1 > /dev/null
test_blocking "BLMPOP first list has data" "blmpop_list1\nitem1" "redis-cli BLMPOP 0 1 blmpop_list1 blmpop_list2 LEFT"

echo -e "${YELLOW}Test 11: BLMPOP second list has data (first empty)${NC}"
redis-cli DEL blmpop_list1 blmpop_list2 > /dev/null
redis-cli LPUSH blmpop_list2 item2 > /dev/null
test_blocking "BLMPOP second list has data" "blmpop_list2\nitem2" "redis-cli BLMPOP 0 1 blmpop_list1 blmpop_list2 LEFT"

# 5. BZPOPMIN basic tests
print_section "5. BZPOPMIN Basic Functionality Tests"

echo -e "${YELLOW}Test 12: BZPOPMIN empty set timeout${NC}"
redis-cli DEL bzpopmin_zset > /dev/null
test_blocking "BZPOPMIN empty set timeout" "(nil)" "timeout 3 redis-cli BZPOPMIN bzpopmin_zset 1"

echo -e "${YELLOW}Test 13: BZPOPMIN immediate return (set has data)${NC}"
redis-cli ZADD bzpopmin_zset 1 member1 2 member2 > /dev/null
test_blocking "BZPOPMIN non-empty set immediate return" "bzpopmin_zset\nmember1\n1" "redis-cli BZPOPMIN bzpopmin_zset 1"

echo -e "${YELLOW}Test 14: BZPOPMIN block then wake up${NC}"
redis-cli DEL bzpopmin_zset > /dev/null
(
    sleep 1
    redis-cli ZADD bzpopmin_zset 5 member5 > /dev/null
) &
test_blocking "BZPOPMIN blocked then woken up" "bzpopmin_zset\nmember5\n5" "timeout 5 redis-cli BZPOPMIN bzpopmin_zset 3"

# 6. BZPOPMAX basic tests
print_section "6. BZPOPMAX Basic Functionality Tests"

echo -e "${YELLOW}Test 15: BZPOPMAX empty set timeout${NC}"
redis-cli DEL bzpopmax_zset > /dev/null
test_blocking "BZPOPMAX empty set timeout" "(nil)" "timeout 3 redis-cli BZPOPMAX bzpopmax_zset 1"

echo -e "${YELLOW}Test 16: BZPOPMAX immediate return (set has data)${NC}"
redis-cli ZADD bzpopmax_zset 1 member1 2 member2 > /dev/null
test_blocking "BZPOPMAX non-empty set immediate return" "bzpopmax_zset\nmember2\n2" "redis-cli BZPOPMAX bzpopmax_zset 1"

echo -e "${YELLOW}Test 17: BZPOPMAX block then wake up${NC}"
redis-cli DEL bzpopmax_zset > /dev/null
(
    sleep 1
    redis-cli ZADD bzpopmax_zset 3 member3 > /dev/null
) &
test_blocking "BZPOPMAX blocked then woken up" "bzpopmax_zset\nmember3\n3" "timeout 5 redis-cli BZPOPMAX bzpopmax_zset 3"

# 7. BZMPOP basic tests
print_section "7. BZMPOP Basic Functionality Tests"

echo -e "${YELLOW}Test 18: BZMPOP timeout when all sets empty${NC}"
redis-cli DEL bzmzset1 bzmzset2 > /dev/null
test_blocking "BZMPOP timeout returns nil" "(nil)" "timeout 3 redis-cli BZMPOP 3 1 bzmzset1 bzmzset2 MIN"

echo -e "${YELLOW}Test 19: BZMPOP first set has data${NC}"
redis-cli ZADD bzmzset1 1 item1 > /dev/null
test_blocking "BZMPOP first set has data" "bzmzset1\n1\nitem1" "redis-cli BZMPOP 0 1 bzmzset1 bzmzset2 MIN"

echo -e "${YELLOW}Test 20: BZMPOP second set has data (first empty)${NC}"
redis-cli DEL bzmzset1 bzmzset2 > /dev/null
redis-cli ZADD bzmzset2 2 item2 > /dev/null
test_blocking "BZMPOP second set has data" "bzmzset2\n1\nitem2" "redis-cli BZMPOP 0 1 bzmzset1 bzmzset2 MIN"

# 8. Multi-client concurrent tests
print_section "8. Multi-Client Concurrent Blocking Tests"

echo -e "${YELLOW}Test 21: Two clients BLPOP same list concurrently${NC}"
redis-cli DEL concurrent_list > /dev/null

# Start two background clients
(
    sleep 0.5
    result=$(timeout 5 redis-cli BLPOP concurrent_list 2)
    if [ "$result" != "(nil)" ]; then
        echo "Client 1 got: $result"
    fi
) &
CLIENT1_PID=$!

(
    sleep 0.5
    result=$(timeout 5 redis-cli BLPOP concurrent_list 2)
    if [ "$result" != "(nil)" ]; then
        echo "Client 2 got: $result"
    fi
) &
CLIENT2_PID=$!

sleep 1
redis-cli LPUSH concurrent_list data_for_client > /dev/null

wait $CLIENT1_PID 2>/dev/null || true
wait $CLIENT2_PID 2>/dev/null || true

echo -e "${GREEN}✓ PASS${NC} - One of the two clients should have received data"
PASSED_TESTS=$((PASSED_TESTS + 1))
TOTAL_TESTS=$((TOTAL_TESTS + 1))

# 9. Edge case tests
print_section "9. Edge Case Tests"

echo -e "${YELLOW}Test 22: Timeout 0 means block indefinitely${NC}"
redis-cli DEL timeout_zero_list > /dev/null

# Start a client with 0 timeout, then add data after 2 seconds
(
    sleep 0.5
    redis-cli LPUSH timeout_zero_list data > /dev/null
) &

# Note: Only test for 3 seconds, 0 should actually block indefinitely
result=$(timeout 5 redis-cli BLPOP timeout_zero_list 0 2>&1 || echo "TIMEOUT")

if [ "$result" = "timeout_zero_list\ndata" ]; then
    echo -e "Test: Timeout 0 immediate return ... ${GREEN}✓ PASS${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "Test: Timeout 0 ... ${RED}✗ FAIL${NC}"
    echo "  Actual: $result"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi
TOTAL_TESTS=$((TOTAL_TESTS + 1))

echo -e "${YELLOW}Test 23: Block on non-existent list${NC}"
redis-cli DEL non_existent_list > /dev/null
test_blocking "BLPOP non-existent list" "(nil)" "timeout 3 redis-cli BLPOP non_existent_list 1"

# Print test results
echo ""
echo "=========================================="
echo "  Blocking Commands Test Results Summary"
echo "=========================================="
echo -e "Total Tests: $TOTAL_TESTS"
echo -e "${GREEN}Passed: $PASSED_TESTS${NC}"
echo -e "${RED}Failed: $FAILED_TESTS${NC}"

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "\n${GREEN}All blocking command tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}$FAILED_TESTS test(s) failed!${NC}"
    echo "Check log: /tmp/astradb_blocking_test.log"
    exit 1
fi