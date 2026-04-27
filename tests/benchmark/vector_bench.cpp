// ==============================================================================
// Vector Search Performance Benchmarks
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <benchmark/benchmark.h>

#include <random>
#include <string>
#include <vector>

#include "astra/container/vector_index_manager.hpp"
#include "astra/container/vector_types.hpp"

namespace {

using astra::container::DistanceMetric;
using astra::container::IndexConfig;

std::mt19937& GetRng() {
  static std::mt19937 rng(42);
  return rng;
}

std::vector<float> RandomVector(size_t dim) {
  std::normal_distribution<float> dist(0.0f, 1.0f);
  std::vector<float> vec(dim);
  for (size_t i = 0; i < dim; ++i) vec[i] = dist(GetRng());
  return vec;
}

std::vector<std::vector<float>> RandomDataset(size_t count, size_t dim) {
  std::vector<std::vector<float>> ds(count);
  for (size_t i = 0; i < count; ++i) ds[i] = RandomVector(dim);
  return ds;
}

IndexConfig MakeConfig(const std::string& name, size_t dim) {
  IndexConfig c;
  c.name = name;
  c.dimension = static_cast<uint32_t>(dim);
  c.distance_metric = DistanceMetric::kCosine;
  c.M = 16;
  c.ef_construction = 200;
  c.ef_search = 100;
  return c;
}

}  // namespace

// ==============================================================================
// Index Lifecycle
// ==============================================================================

static void BM_IndexCreate(benchmark::State& state) {
  const size_t dim = static_cast<size_t>(state.range(0));
  for (auto _ : state) {
    astra::container::VectorIndexManager mgr;
    auto ok = mgr.CreateIndex(MakeConfig("idx", dim));
    benchmark::DoNotOptimize(ok);
  }
  state.SetLabel(std::to_string(dim) + "d");
}
BENCHMARK(BM_IndexCreate)
    ->Args({256, 512, 768, 1024, 1536})
    ->Unit(benchmark::kMicrosecond);

// ==============================================================================
// Single Insert Latency
// ==============================================================================

static void BM_InsertLatency(benchmark::State& state) {
  const size_t dim = static_cast<size_t>(state.range(0));
  for (auto _ : state) {
    state.PauseTiming();
    astra::container::VectorIndexManager mgr;
    mgr.CreateIndex(MakeConfig("idx", dim));
    auto vec = RandomVector(dim);
    state.ResumeTiming();
    mgr.AddVector("idx", 1, vec.data());
  }
  state.SetLabel(std::to_string(dim) + "d");
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_InsertLatency)
    ->Args({256, 512, 768, 1024, 1536})
    ->Unit(benchmark::kMicrosecond);

// ==============================================================================
// Bulk Build Throughput (vectors/second)
// ==============================================================================

static void BM_BulkBuild(benchmark::State& state) {
  const size_t count = static_cast<size_t>(state.range(0));
  const size_t dim = 768;
  auto dataset = RandomDataset(count, dim);

  for (auto _ : state) {
    state.PauseTiming();
    astra::container::VectorIndexManager mgr;
    mgr.CreateIndex(MakeConfig("idx", dim));
    state.ResumeTiming();
    for (size_t i = 0; i < count; ++i) {
      mgr.AddVector("idx", i, dataset[i].data());
    }
  }
  state.SetItemsProcessed(state.iterations() * count);
  state.SetLabel(std::to_string(count) + "x768d");
}
BENCHMARK(BM_BulkBuild)
    ->Args({1000, 10000, 50000, 100000})
    ->Unit(benchmark::kMillisecond);

// ==============================================================================
// Search Latency (KNN, single query)
// ==============================================================================

static void BM_SearchLatency(benchmark::State& state) {
  const size_t count = static_cast<size_t>(state.range(0));
  const size_t dim = 768;
  const size_t k = static_cast<size_t>(state.range(1));

  astra::container::VectorIndexManager mgr;
  mgr.CreateIndex(MakeConfig("idx", dim));
  auto dataset = RandomDataset(count, dim);
  for (size_t i = 0; i < count; ++i) {
    mgr.AddVector("idx", i, dataset[i].data());
  }

  auto queries = RandomDataset(256, dim);
  size_t qi = 0;
  float sink = 0;

  for (auto _ : state) {
    auto results = mgr.SearchKNN("idx", queries[qi % 256].data(), k);
    if (!results.empty()) sink += results[0].second;
    qi++;
  }

  benchmark::DoNotOptimize(sink);
  state.SetItemsProcessed(state.iterations());
  state.SetLabel(std::to_string(count) + "x" + std::to_string(dim) +
                 "d k=" + std::to_string(k));
}
BENCHMARK(BM_SearchLatency)
    ->ArgsProduct({{1000, 10000, 50000, 100000}, {1, 10, 50, 100}})
    ->Unit(benchmark::kMicrosecond);

// ==============================================================================
// Search Throughput (QPS)
// ==============================================================================

static void BM_SearchThroughput(benchmark::State& state) {
  const size_t count = static_cast<size_t>(state.range(0));
  const size_t dim = 768;
  const size_t k = 10;

  astra::container::VectorIndexManager mgr;
  mgr.CreateIndex(MakeConfig("idx", dim));
  auto dataset = RandomDataset(count, dim);
  for (size_t i = 0; i < count; ++i) {
    mgr.AddVector("idx", i, dataset[i].data());
  }

  auto queries = RandomDataset(1024, dim);
  size_t qi = 0;
  float sink = 0;

  for (auto _ : state) {
    auto results = mgr.SearchKNN("idx", queries[qi++ % 1024].data(), k);
    if (!results.empty()) sink += results[0].second;
  }

  benchmark::DoNotOptimize(sink);
  state.SetItemsProcessed(state.iterations());
  state.SetLabel(std::to_string(count) + "x768d k=10");
}
BENCHMARK(BM_SearchThroughput)
    ->Args({1000, 10000, 50000, 100000})
    ->Unit(benchmark::kMicrosecond);

// ==============================================================================
// Mixed R/W (simulated RAG workload: 80% reads, 20% writes)
// ==============================================================================

static void BM_RAGWorkload(benchmark::State& state) {
  const size_t count = static_cast<size_t>(state.range(0));
  const size_t dim = 768;
  const size_t k = 10;

  astra::container::VectorIndexManager mgr;
  mgr.CreateIndex(MakeConfig("idx", dim));
  auto dataset = RandomDataset(count, dim);
  for (size_t i = 0; i < count; ++i) {
    mgr.AddVector("idx", i, dataset[i].data());
  }

  auto queries = RandomDataset(128, dim);
  auto writes = RandomDataset(128, dim);
  size_t qi = 0, wi = 0, next_id = count;
  std::uniform_int_distribution<int> op_dist(0, 99);
  float sink = 0;

  for (auto _ : state) {
    if (op_dist(GetRng()) < 80) {
      auto results = mgr.SearchKNN("idx", queries[qi++ % 128].data(), k);
      if (!results.empty()) sink += results[0].second;
    } else {
      mgr.AddVector("idx", next_id++, writes[wi++ % 128].data());
    }
  }

  benchmark::DoNotOptimize(sink);
  state.SetLabel(std::to_string(count) + "x768d r/w=80/20");
}
BENCHMARK(BM_RAGWorkload)
    ->Args({1000, 10000, 100000})
    ->Unit(benchmark::kMicrosecond);

// ==============================================================================
// Raw Distance Computation (scalar — baseline for future SIMD)
// ==============================================================================

static void BM_L2Distance(benchmark::State& state) {
  const size_t dim = static_cast<size_t>(state.range(0));
  auto a = RandomVector(dim);
  auto b = RandomVector(dim);

  for (auto _ : state) {
    float sum = 0;
    for (size_t i = 0; i < dim; ++i) {
      float d = a[i] - b[i];
      sum += d * d;
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations());
  state.SetLabel(std::to_string(dim) + "d");
}
BENCHMARK(BM_L2Distance)
    ->Args({256, 512, 768, 1024, 1536})
    ->Unit(benchmark::kNanosecond);

static void BM_InnerProduct(benchmark::State& state) {
  const size_t dim = static_cast<size_t>(state.range(0));
  auto a = RandomVector(dim);
  auto b = RandomVector(dim);

  for (auto _ : state) {
    float sum = 0;
    for (size_t i = 0; i < dim; ++i) sum += a[i] * b[i];
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations());
  state.SetLabel(std::to_string(dim) + "d");
}
BENCHMARK(BM_InnerProduct)
    ->Args({256, 512, 768, 1024, 1536})
    ->Unit(benchmark::kNanosecond);

// ==============================================================================
// Multi-Index Search
// ==============================================================================

static void BM_MultiIndexSearch(benchmark::State& state) {
  const size_t n_indexes = static_cast<size_t>(state.range(0));
  const size_t dim = 768;
  const size_t per_index = 500;

  astra::container::VectorIndexManager mgr;
  auto queries = RandomDataset(64, dim);
  for (size_t idx = 0; idx < n_indexes; ++idx) {
    auto name = "idx" + std::to_string(idx);
    mgr.CreateIndex(MakeConfig(name, dim));
    for (size_t i = 0; i < per_index; ++i) {
      mgr.AddVector(name, i, RandomVector(dim).data());
    }
  }

  size_t qi = 0;
  float sink = 0;
  for (auto _ : state) {
    for (size_t idx = 0; idx < n_indexes; ++idx) {
      auto results =
          mgr.SearchKNN("idx" + std::to_string(idx),
                         queries[qi++ % 64].data(), 10);
      if (!results.empty()) sink += results[0].second;
    }
  }
  benchmark::DoNotOptimize(sink);
  state.SetItemsProcessed(state.iterations() * n_indexes);
  state.SetLabel(std::to_string(n_indexes) + " idx x" +
                 std::to_string(per_index) + "v");
}
BENCHMARK(BM_MultiIndexSearch)
    ->Args({1, 2, 4, 8})
    ->Unit(benchmark::kMicrosecond);
