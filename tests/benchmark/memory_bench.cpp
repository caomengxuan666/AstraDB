// ==============================================================================
// Memory Benchmarks
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <benchmark/benchmark.h>

#include <string>
#include <vector>

#include "astra/core/memory/arena_allocator.hpp"
#include "astra/core/memory/buffer_pool.hpp"
#include "astra/core/memory/object_pool.hpp"

namespace astra::core::memory {

static void BM_ArenaAllocate(benchmark::State& state) {
  Arena arena(1024 * 1024);

  for (auto _ : state) {
    void* ptr = arena.Allocate(state.range(0));
    benchmark::DoNotOptimize(ptr);
  }

  state.SetBytesProcessed(state.iterations() * state.range(0));
}

static void BM_ArenaAllocateAligned(benchmark::State& state) {
  Arena arena(1024 * 1024);

  for (auto _ : state) {
    void* ptr = arena.AllocateAligned(state.range(0), 64);
    benchmark::DoNotOptimize(ptr);
  }

  state.SetBytesProcessed(state.iterations() * state.range(0));
}

static void BM_MallocAllocate(benchmark::State& state) {
  for (auto _ : state) {
    void* ptr = malloc(state.range(0));
    benchmark::DoNotOptimize(ptr);
    free(ptr);
  }

  state.SetBytesProcessed(state.iterations() * state.range(0));
}

static void BM_BufferPoolAcquire(benchmark::State& state) {
  BufferPool pool;

  for (auto _ : state) {
    auto buffer = pool.Acquire(state.range(0));
    benchmark::DoNotOptimize(buffer);
  }

  state.SetBytesProcessed(state.iterations() * state.range(0));
}

static void BM_BufferDirectAllocate(benchmark::State& state) {
  for (auto _ : state) {
    auto buffer = new Buffer(state.range(0));
    benchmark::DoNotOptimize(buffer);
    delete buffer;
  }

  state.SetBytesProcessed(state.iterations() * state.range(0));
}

static void BM_ObjectPoolAcquire(benchmark::State& state) {
  ObjectPool<int> pool(1000);

  for (auto _ : state) {
    auto obj = pool.Acquire();
    benchmark::DoNotOptimize(obj);
  }
}

static void BM_ObjectNewDelete(benchmark::State& state) {
  for (auto _ : state) {
    auto obj = new int();
    benchmark::DoNotOptimize(obj);
    delete obj;
  }
}

static void BM_ArenaAllocatorVector(benchmark::State& state) {
  Arena arena(1024 * 1024);
  ArenaAllocator<int> allocator(&arena);

  for (auto _ : state) {
    std::vector<int, ArenaAllocator<int>> vec(allocator);
    vec.reserve(state.range(0));
    for (int i = 0; i < state.range(0); ++i) {
      vec.push_back(i);
    }
    benchmark::DoNotOptimize(vec.data());
  }

  state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void BM_StdAllocatorVector(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<int> vec;
    vec.reserve(state.range(0));
    for (int i = 0; i < state.range(0); ++i) {
      vec.push_back(i);
    }
    benchmark::DoNotOptimize(vec.data());
  }

  state.SetItemsProcessed(state.iterations() * state.range(0));
}

// Register benchmarks
BENCHMARK(BM_ArenaAllocate)->Range(64, 4096);
BENCHMARK(BM_ArenaAllocateAligned)->Range(64, 4096);
BENCHMARK(BM_MallocAllocate)->Range(64, 4096);

BENCHMARK(BM_BufferPoolAcquire)->Range(64, 4096);
BENCHMARK(BM_BufferDirectAllocate)->Range(64, 4096);

BENCHMARK(BM_ObjectPoolAcquire);
BENCHMARK(BM_ObjectNewDelete);

BENCHMARK(BM_ArenaAllocatorVector)->Range(100, 10000);
BENCHMARK(BM_StdAllocatorVector)->Range(100, 10000);

}  // namespace astra::core::memory
