#!/bin/bash

# AstraDB vs Redis 性能对比测试

ASTRADB_PORT=6379
REDIS_PORT=6380
REQUESTS=1000
CONCURRENCY=1

echo "========================================"
echo "AstraDB vs Redis 性能对比测试"
echo "========================================"
echo "请求次数: $REQUESTS"
echo "并发数: $CONCURRENCY"
echo ""

# 启动 AstraDB
echo "启动 AstraDB (端口 $ASTRADB_PORT)..."
pkill -f "build-release/bin/astradb" 2>/dev/null
./build-release/bin/astradb > /dev/null 2>&1 &
ASTRADB_PID=$!
sleep 3

# 确保 Redis 运行
echo "确保 Redis 运行 (端口 $REDIS_PORT)..."
pkill redis-server 2>/dev/null
sleep 1
redis-server --port $REDIS_PORT --save "" --appendonly no --daemonize yes
sleep 2

echo ""
echo "========================================"
echo "测试 AstraDB"
echo "========================================"
redis-benchmark -h 127.0.0.1 -p $ASTRADB_PORT -t set,get,hset,hget,sadd,zadd -n $REQUESTS -c $CONCURRENCY -q

echo ""
echo "========================================"
echo "测试 Redis"
echo "========================================"
redis-benchmark -h 127.0.0.1 -p $REDIS_PORT -t set,get,hset,hget,sadd,zadd -n $REQUESTS -c $CONCURRENCY -q

# 清理
echo ""
echo "清理进程..."
kill $ASTRADB_PID 2>/dev/null
redis-cli -p $REDIS_PORT shutdown 2>/dev/null
sleep 1

echo ""
echo "========================================"
echo "测试完成"
echo "========================================"