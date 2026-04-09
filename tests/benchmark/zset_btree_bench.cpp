// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include <benchmark/benchmark.h>

#include <random>
#include <string>
#include <vector>

#include "astra/container/zset/btree_zset.hpp"

namespace astra::container {

// Helper to generate random members
static std::string GenerateMember(int i) {
  return "member" + std::to_string(i);
}

// Helper to generate random scores
static double GenerateScore(int i, std::mt19937& rng) {
  std::uniform_real_distribution<double> dist(0.0, 10000.0);
  return dist(rng);
}

// Benchmark BTree ZSet insertion
static void BM_BTreeZSet_Insert(benchmark::State& state) {
  const int num_elements = state.range(0);
  std::mt19937 rng(42);  // Fixed seed for reproducibility

  for (auto _ : state) {
    StringZSet zset(num_elements);

    for (int i = 0; i < num_elements; ++i) {
      zset.Add(GenerateMember(i), GenerateScore(i, rng));
    }

    benchmark::DoNotOptimize(zset);
  }

  state.SetItemsProcessed(num_elements);
  state.SetBytesProcessed(num_elements * sizeof(std::string) + num_elements * sizeof(double));
}

// Benchmark BTree ZSet lookup by rank
static void BM_BTreeZSet_GetByRank(benchmark::State& state) {
  const int num_elements = state.range(0);
  std::mt19937 rng(42);

  StringZSet zset(num_elements);
  for (int i = 0; i < num_elements; ++i) {
    zset.Add(GenerateMember(i), GenerateScore(i, rng));
  }

  for (auto _ : state) {
    for (int i = 0; i < num_elements; ++i) {
      benchmark::DoNotOptimize(zset.GetByRank(i % num_elements));
    }
  }

  state.SetItemsProcessed(num_elements);
}

// Benchmark BTree ZSet lookup by member
static void BM_BTreeZSet_GetScore(benchmark::State& state) {
  const int num_elements = state.range(0);
  std::mt19937 rng(42);

  StringZSet zset(num_elements);
  for (int i = 0; i < num_elements; ++i) {
    zset.Add(GenerateMember(i), GenerateScore(i, rng));
  }

  for (auto _ : state) {
    for (int i = 0; i < num_elements; ++i) {
      benchmark::DoNotOptimize(zset.GetScore(GenerateMember(i % num_elements)));
    }
  }

  state.SetItemsProcessed(num_elements);
}

// Benchmark BTree ZSet range query by rank
static void BM_BTreeZSet_GetRangeByRank(benchmark::State& state) {
  const int num_elements = state.range(0);
  std::mt19937 rng(42);

  StringZSet zset(num_elements);
  for (int i = 0; i < num_elements; ++i) {
    zset.Add(GenerateMember(i), GenerateScore(i, rng));
  }

  const int range_size = num_elements / 10;
  for (auto _ : state) {
    auto range = zset.GetRangeByRank(0, range_size);
    benchmark::DoNotOptimize(range);
  }

  state.SetItemsProcessed(range_size);
}

// Benchmark BTree ZSet range query by score
static void BM_BTreeZSet_GetRangeByScore(benchmark::State& state) {
  const int num_elements = state.range(0);
  std::mt19937 rng(42);

  StringZSet zset(num_elements);
  for (int i = 0; i < num_elements; ++i) {
    zset.Add(GenerateMember(i), GenerateScore(i, rng));
  }

  for (auto _ : state) {
    auto range = zset.GetRangeByScore(1000.0, 2000.0);
    benchmark::DoNotOptimize(range);
  }

  state.SetItemsProcessed(state.range(0) / 10);  // Approximate
}

// Benchmark BTree ZSet remove
static void BM_BTreeZSet_Remove(benchmark::State& state) {
  const int num_elements = state.range(0);
  std::mt19937 rng(42);

  for (auto _ : state) {
    StringZSet zset(num_elements);
    for (int i = 0; i < num_elements; ++i) {
      zset.Add(GenerateMember(i), GenerateScore(i, rng));
    }

    for (int i = 0; i < num_elements; ++i) {
      zset.Remove(GenerateMember(i));
    }

    benchmark::DoNotOptimize(zset);
  }

  state.SetItemsProcessed(num_elements);
}

// Benchmark BTree ZSet update (add existing member)
static void BM_BTreeZSet_Update(benchmark::State& state) {
  const int num_elements = state.range(0);
  std::mt19937 rng(42);

  StringZSet zset(num_elements);
  for (int i = 0; i < num_elements; ++i) {
    zset.Add(GenerateMember(i), GenerateScore(i, rng));
  }

  for (auto _ : state) {
    for (int i = 0; i < num_elements; ++i) {
      zset.Add(GenerateMember(i), GenerateScore(i, rng) * 2.0);
    }
  }

  state.SetItemsProcessed(num_elements);
}

// Benchmark BTree ZSet GetRank
static void BM_BTreeZSet_GetRank(benchmark::State& state) {
  const int num_elements = state.range(0);
  std::mt19937 rng(42);

  StringZSet zset(num_elements);
  for (int i = 0; i < num_elements; ++i) {
    zset.Add(GenerateMember(i), GenerateScore(i, rng));
  }

  for (auto _ : state) {
    for (int i = 0; i < num_elements; ++i) {
      benchmark::DoNotOptimize(zset.GetRank(GenerateMember(i % num_elements)));
    }
  }

  state.SetItemsProcessed(num_elements);
}

// ==================== Register Benchmarks ====================

// Small dataset benchmarks
BENCHMARK(BM_BTreeZSet_Insert)->Range(100, 10000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BTreeZSet_GetByRank)->Range(100, 10000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BTreeZSet_GetScore)->Range(100, 10000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BTreeZSet_GetRangeByRank)->Range(1000, 100000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BTreeZSet_GetRangeByScore)->Range(1000, 100000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BTreeZSet_Remove)->Range(100, 10000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BTreeZSet_Update)->Range(100, 10000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BTreeZSet_GetRank)->Range(100, 10000)->Unit(benchmark::kMicrosecond);

// Large dataset benchmarks
BENCHMARK(BM_BTreeZSet_Insert)->Arg(100000)->Arg(500000)->Arg(1000000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_BTreeZSet_GetByRank)->Arg(100000)->Arg(500000)->Arg(1000000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_BTreeZSet_GetScore)->Arg(100000)->Arg(500000)->Arg(1000000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_BTreeZSet_GetRangeByRank)->Arg(100000)->Arg(500000)->Arg(1000000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_BTreeZSet_GetRangeByScore)->Arg(100000)->Arg(500000)->Arg(1000000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_BTreeZSet_Remove)->Arg(100000)->Arg(500000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_BTreeZSet_Update)->Arg(100000)->Arg(500000)->Arg(1000000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_BTreeZSet_GetRank)->Arg(100000)->Arg(500000)->Arg(1000000)->Unit(benchmark::kMillisecond);

}  // namespace astra::container