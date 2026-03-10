// ==============================================================================
// Persistence Benchmarks
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <benchmark/benchmark.h>

#include <filesystem>
#include <random>
#include <thread>

#include "astra/cluster/shard_manager.hpp"
#include "astra/persistence/leveldb_adapter.hpp"

namespace astra::persistence {
namespace {

// Import cluster types for benchmarks
using astra::cluster::HashSlotCalculator;
using astra::cluster::kHashSlotCount;
using astra::cluster::NodeId;
using astra::cluster::ShardId;
using astra::cluster::ShardManager;

// Generate random string of given length
std::string RandomString(size_t length) {
  static const char chars[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  static std::mt19937 rng(std::random_device{}());
  static std::uniform_int_distribution<size_t> dist(0, sizeof(chars) - 2);

  std::string result;
  result.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    result += chars[dist(rng)];
  }
  return result;
}

// Benchmark fixture for LevelDB
class LevelDBBenchmark : public benchmark::Fixture {
 public:
  void SetUp(const ::benchmark::State& state) override {
    test_dir_ =
        "/tmp/astradb_bench_leveldb_" + std::to_string(std::time(nullptr));
    std::filesystem::create_directories(test_dir_);

    LevelDBOptions options;
    options.db_path = test_dir_;
    options.create_if_missing = true;
    options.error_if_exists = false;
    options.cache_size = 256 * 1024 * 1024;        // 256MB cache
    options.write_buffer_size = 64 * 1024 * 1024;  // 64MB write buffer

    adapter_.Open(options);
  }

  void TearDown(const ::benchmark::State& state) override {
    adapter_.Close();
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
  }

  LevelDBAdapter adapter_;
  std::string test_dir_;
};

// ========== Put Benchmarks ==========

BENCHMARK_DEFINE_F(LevelDBBenchmark, Put)(benchmark::State& state) {
  const size_t key_size = state.range(0);
  const size_t value_size = state.range(1);

  size_t i = 0;
  for (auto _ : state) {
    std::string key = "key_" + std::to_string(i);
    std::string value = RandomString(value_size);

    adapter_.Put(key, value);
    ++i;
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(state.iterations() * (key_size + value_size));
}

BENCHMARK_REGISTER_F(LevelDBBenchmark, Put)
    ->Args({16, 64})     // Small key, small value
    ->Args({16, 1024})   // Small key, 1KB value
    ->Args({16, 65536})  // Small key, 64KB value
    ->Args({64, 64})     // Medium key, small value
    ->Args({64, 1024});  // Medium key, 1KB value

// ========== Get Benchmarks ==========

BENCHMARK_DEFINE_F(LevelDBBenchmark, Get)(benchmark::State& state) {
  const size_t value_size = state.range(0);
  const size_t num_keys = 10000;

  // Pre-populate
  for (size_t i = 0; i < num_keys; ++i) {
    adapter_.Put("key_" + std::to_string(i), RandomString(value_size));
  }

  size_t i = 0;
  for (auto _ : state) {
    std::string key = "key_" + std::to_string(i % num_keys);
    adapter_.Get(key);
    ++i;
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(LevelDBBenchmark, Get)
    ->Args({64})
    ->Args({1024})
    ->Args({65536});

// ========== Batch Write Benchmarks ==========

BENCHMARK_DEFINE_F(LevelDBBenchmark, WriteBatch)(benchmark::State& state) {
  const size_t batch_size = state.range(0);
  const size_t value_size = state.range(1);

  size_t batch_count = 0;
  for (auto _ : state) {
    LevelDBAdapter::WriteBatch batch;
    for (size_t i = 0; i < batch_size; ++i) {
      batch.Put(
          "batch_" + std::to_string(batch_count) + "_" + std::to_string(i),
          RandomString(value_size));
    }
    adapter_.Write(batch);
    ++batch_count;
  }

  state.SetItemsProcessed(state.iterations() * batch_size);
  state.SetBytesProcessed(state.iterations() * batch_size * value_size);
}

BENCHMARK_REGISTER_F(LevelDBBenchmark, WriteBatch)
    ->Args({10, 64})
    ->Args({100, 64})
    ->Args({1000, 64})
    ->Args({100, 1024});

// ========== Mixed Read/Write Benchmark ==========

BENCHMARK_DEFINE_F(LevelDBBenchmark, MixedReadWrite)(benchmark::State& state) {
  const size_t read_percent = state.range(0);
  const size_t num_keys = 10000;

  // Pre-populate
  for (size_t i = 0; i < num_keys; ++i) {
    adapter_.Put("key_" + std::to_string(i), RandomString(256));
  }

  static std::mt19937 rng(std::random_device{}());
  static std::uniform_int_distribution<size_t> key_dist(0, num_keys - 1);
  static std::uniform_int_distribution<size_t> op_dist(0, 99);

  size_t reads = 0, writes = 0;
  for (auto _ : state) {
    size_t op = op_dist(rng);
    size_t key_idx = key_dist(rng);
    std::string key = "key_" + std::to_string(key_idx);

    if (op < read_percent) {
      adapter_.Get(key);
      ++reads;
    } else {
      adapter_.Put(key, RandomString(256));
      ++writes;
    }
  }

  state.counters["reads"] = reads;
  state.counters["writes"] = writes;
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(LevelDBBenchmark, MixedReadWrite)
    ->Args({80})   // 80% reads, 20% writes
    ->Args({50})   // 50% reads, 50% writes
    ->Args({20});  // 20% reads, 80% writes

// ========== Concurrent Access Benchmark ==========

BENCHMARK_DEFINE_F(LevelDBBenchmark, ConcurrentPut)(benchmark::State& state) {
  const size_t num_threads = state.range(0);

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  auto worker = [this, &state](int thread_id) {
    size_t i = 0;
    while (state.KeepRunning()) {
      std::string key =
          "t" + std::to_string(thread_id) + "_key_" + std::to_string(i);
      adapter_.Put(key, RandomString(256));
      ++i;
    }
  };

  for (size_t t = 0; t < num_threads; ++t) {
    threads.emplace_back(worker, t);
  }

  for (auto& t : threads) {
    t.join();
  }

  state.SetItemsProcessed(state.iterations() * num_threads);
}

BENCHMARK_REGISTER_F(LevelDBBenchmark, ConcurrentPut)
    ->Args({1})
    ->Args({2})
    ->Args({4})
    ->Args({8});

// ========== Key Encoding Benchmark ==========

BENCHMARK_F(LevelDBBenchmark, EncodeKeyString)(benchmark::State& state) {
  std::string key = "my_test_key";
  for (auto _ : state) {
    auto encoded = LevelDBAdapter::EncodeKey(KeyPrefix::kString, key);
    benchmark::DoNotOptimize(encoded);
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_F(LevelDBBenchmark, EncodeHashKey)(benchmark::State& state) {
  std::string key = "hash_key";
  std::string field = "field_name";
  for (auto _ : state) {
    auto encoded = LevelDBAdapter::EncodeHashKey(key, field);
    benchmark::DoNotOptimize(encoded);
  }
  state.SetItemsProcessed(state.iterations());
}

// ========== Slot Calculation Benchmark ==========

void BM_CalculateSlot(benchmark::State& state) {
  std::string key = "benchmark_test_key_12345";
  for (auto _ : state) {
    auto slot = HashSlotCalculator::Calculate(key);
    benchmark::DoNotOptimize(slot);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CalculateSlot);

void BM_CalculateSlotWithHashTag(benchmark::State& state) {
  std::string key = "{user:12345}:profile";
  for (auto _ : state) {
    auto slot = HashSlotCalculator::CalculateWithTag(key);
    benchmark::DoNotOptimize(slot);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CalculateSlotWithHashTag);

// ========== Shard Lookup Benchmark ==========

void BM_GetShardForKey(benchmark::State& state) {
  ShardManager manager;
  NodeId self_id{};
  std::fill(self_id.begin(), self_id.end(), 0x01);
  manager.Init(16, self_id);

  std::string key = "benchmark_shard_key";
  for (auto _ : state) {
    ShardId shard = manager.GetShardForKey(key);
    benchmark::DoNotOptimize(shard);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_GetShardForKey);

}  // namespace
}  // namespace astra::persistence
