#include <benchmark/benchmark.h>

#include <string>
#include <vector>

#include "astra/container/zset/btree_zset.hpp"

namespace astra::container {

// Benchmark ZSet insertion
static void BM_ZSet_Insert(benchmark::State& state) {
  const int num_elements = state.range(0);

  for (auto _ : state) {
    StringZSet zset(num_elements);

    for (int i = 0; i < num_elements; ++i) {
      zset.Add("member" + std::to_string(i), static_cast<double>(i));
    }

    benchmark::DoNotOptimize(zset);
  }

  state.SetItemsProcessed(num_elements);
}

BENCHMARK(BM_ZSet_Insert)->Range(100, 10000)->Unit(benchmark::kMicrosecond);

// Benchmark ZSet lookup by rank
static void BM_ZSet_GetByRank(benchmark::State& state) {
  const int num_elements = state.range(0);

  StringZSet zset(num_elements);
  for (int i = 0; i < num_elements; ++i) {
    zset.Add("member" + std::to_string(i), static_cast<double>(i));
  }

  for (auto _ : state) {
    for (int i = 0; i < num_elements; ++i) {
      benchmark::DoNotOptimize(zset.GetByRank(i));
    }
  }

  state.SetItemsProcessed(num_elements);
}

BENCHMARK(BM_ZSet_GetByRank)->Range(100, 10000)->Unit(benchmark::kMicrosecond);

// Benchmark ZSet lookup by score
static void BM_ZSet_GetScore(benchmark::State& state) {
  const int num_elements = state.range(0);

  StringZSet zset(num_elements);
  for (int i = 0; i < num_elements; ++i) {
    zset.Add("member" + std::to_string(i), static_cast<double>(i));
  }

  for (auto _ : state) {
    for (int i = 0; i < num_elements; ++i) {
      benchmark::DoNotOptimize(zset.GetScore("member" + std::to_string(i)));
    }
  }

  state.SetItemsProcessed(num_elements);
}

BENCHMARK(BM_ZSet_GetScore)->Range(100, 10000)->Unit(benchmark::kMicrosecond);

// Benchmark ZSet range query
static void BM_ZSet_GetRangeByRank(benchmark::State& state) {
  const int num_elements = state.range(0);

  StringZSet zset(num_elements);
  for (int i = 0; i < num_elements; ++i) {
    zset.Add("member" + std::to_string(i), static_cast<double>(i));
  }

  for (auto _ : state) {
    auto range = zset.GetRangeByRank(0, num_elements / 10);
    benchmark::DoNotOptimize(range);
  }

  state.SetItemsProcessed(state.range(0) / 10);
}

BENCHMARK(BM_ZSet_GetRangeByRank)
    ->Range(1000, 100000)
    ->Unit(benchmark::kMicrosecond);

// Benchmark ZSet remove
static void BM_ZSet_Remove(benchmark::State& state) {
  const int num_elements = state.range(0);

  for (auto _ : state) {
    StringZSet zset(num_elements);
    for (int i = 0; i < num_elements; ++i) {
      zset.Add("member" + std::to_string(i), static_cast<double>(i));
    }

    for (int i = 0; i < num_elements; ++i) {
      zset.Remove("member" + std::to_string(i));
    }

    benchmark::DoNotOptimize(zset);
  }

  state.SetItemsProcessed(num_elements);
}

BENCHMARK(BM_ZSet_Remove)->Range(100, 10000)->Unit(benchmark::kMicrosecond);

// Benchmark ZSet update (add existing member)
static void BM_ZSet_Update(benchmark::State& state) {
  const int num_elements = state.range(0);

  StringZSet zset(num_elements);
  for (int i = 0; i < num_elements; ++i) {
    zset.Add("member" + std::to_string(i), static_cast<double>(i));
  }

  for (auto _ : state) {
    for (int i = 0; i < num_elements; ++i) {
      zset.Add("member" + std::to_string(i), static_cast<double>(i) * 2.0);
    }
  }

  state.SetItemsProcessed(num_elements);
}

BENCHMARK(BM_ZSet_Update)->Range(100, 10000)->Unit(benchmark::kMicrosecond);

}  // namespace astra::container
