#include <benchmark/benchmark.h>

#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "astra/persistence/rocksdb_adapter.hpp"

using namespace astra::persistence;

// Helper function to clean up test database
void CleanupDB(const std::string& db_path) {
  std::filesystem::remove_all(db_path);
}

// Benchmark: Single-threaded sequential write
static void BM_RocksDB_SequentialWrite(benchmark::State& state) {
  const std::string db_path = "/tmp/rocksdb_bench_sequential_write";
  CleanupDB(db_path);

  RocksDBAdapter::Config config;
  config.db_path = db_path;
  config.cache_size = 128 * 1024 * 1024;  // 128MB
  config.enable_wal = true;
  config.enable_compression = false;  // Disable for fair comparison

  RocksDBAdapter db(config);

  size_t counter = 0;
  for (auto _ : state) {
    std::string key = "key_" + std::to_string(counter);
    std::string value = "value_" + std::to_string(counter);
    db.Put(key, value);
    counter++;
  }

  state.SetItemsProcessed(counter);
  CleanupDB(db_path);
}

// Benchmark: Single-threaded sequential read
static void BM_RocksDB_SequentialRead(benchmark::State& state) {
  const std::string db_path = "/tmp/rocksdb_bench_sequential_read";
  CleanupDB(db_path);

  RocksDBAdapter::Config config;
  config.db_path = db_path;
  config.cache_size = 128 * 1024 * 1024;
  config.enable_wal = true;
  config.enable_compression = false;

  RocksDBAdapter db(config);

  // Pre-populate data
  const size_t num_keys = state.range(0);
  for (size_t i = 0; i < num_keys; ++i) {
    std::string key = "key_" + std::to_string(i);
    std::string value = "value_" + std::to_string(i);
    db.Put(key, value);
  }

  // Benchmark reads
  size_t counter = 0;
  for (auto _ : state) {
    std::string key = "key_" + std::to_string(counter % num_keys);
    auto result = db.Get(key);
    benchmark::DoNotOptimize(result);
    counter++;
  }

  state.SetItemsProcessed(counter);
  CleanupDB(db_path);
}

// Benchmark: Random read
static void BM_RocksDB_RandomRead(benchmark::State& state) {
  const std::string db_path = "/tmp/rocksdb_bench_random_read";
  CleanupDB(db_path);

  RocksDBAdapter::Config config;
  config.db_path = db_path;
  config.cache_size = 128 * 1024 * 1024;
  config.enable_wal = true;
  config.enable_compression = false;

  RocksDBAdapter db(config);

  // Pre-populate data
  const size_t num_keys = state.range(0);
  for (size_t i = 0; i < num_keys; ++i) {
    std::string key = "key_" + std::to_string(i);
    std::string value = "value_" + std::to_string(i);
    db.Put(key, value);
  }

  // Benchmark random reads
  for (auto _ : state) {
    size_t idx = rand() % num_keys;
    std::string key = "key_" + std::to_string(idx);
    auto result = db.Get(key);
    benchmark::DoNotOptimize(result);
  }

  state.SetItemsProcessed(state.iterations());
  CleanupDB(db_path);
}

// Benchmark: Batch write
static void BM_RocksDB_BatchWrite(benchmark::State& state) {
  const std::string db_path = "/tmp/rocksdb_bench_batch_write";
  CleanupDB(db_path);

  RocksDBAdapter::Config config;
  config.db_path = db_path;
  config.cache_size = 128 * 1024 * 1024;
  config.enable_wal = true;
  config.enable_compression = false;

  RocksDBAdapter db(config);

  size_t counter = 0;
  const size_t batch_size = 1000;

  for (auto _ : state) {
    std::vector<std::pair<std::string, std::string>> batch;
    for (size_t i = 0; i < batch_size; ++i) {
      std::string key = "key_" + std::to_string(counter);
      std::string value = "value_" + std::to_string(counter);
      batch.emplace_back(key, value);
      counter++;
    }
    db.BatchPut(batch);
  }

  state.SetItemsProcessed(counter);
  CleanupDB(db_path);
}

// Benchmark: Compression impact
static void BM_RocksDB_CompressionWrite(benchmark::State& state) {
  const std::string db_path = "/tmp/rocksdb_bench_compression_write";
  CleanupDB(db_path);

  RocksDBAdapter::Config config;
  config.db_path = db_path;
  config.cache_size = 128 * 1024 * 1024;
  config.enable_wal = true;
  config.enable_compression = true;
  config.compression_type =
      static_cast<rocksdb::CompressionType>(state.range(0));

  RocksDBAdapter db(config);

  size_t counter = 0;
  for (auto _ : state) {
    std::string key = "key_" + std::to_string(counter);
    std::string value =
        "value_" + std::to_string(counter) + "_data_" + std::string(100, 'x');
    db.Put(key, value);
    counter++;
  }

  state.SetItemsProcessed(counter);
  CleanupDB(db_path);
}

// Benchmark: Multi-threaded write (simulating NO SHADING architecture)
static void BM_RocksDB_MultiThreadWrite(benchmark::State& state) {
  const int num_threads = state.range(0);
  const std::string db_path = "/tmp/rocksdb_bench_multithread_write";
  CleanupDB(db_path);

  // Each thread has its own RocksDB instance (NO SHADING)
  std::vector<std::unique_ptr<RocksDBAdapter>> dbs;
  for (int i = 0; i < num_threads; ++i) {
    std::string thread_db_path = db_path + "_thread_" + std::to_string(i);
    RocksDBAdapter::Config config;
    config.db_path = thread_db_path;
    config.cache_size = 64 * 1024 * 1024;  // 64MB per thread
    config.enable_wal = true;
    config.enable_compression = false;
    dbs.push_back(std::make_unique<RocksDBAdapter>(config));
  }

  // Launch threads
  std::vector<std::thread> threads;
  std::atomic<size_t> total_ops{0};

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&, t]() {
      size_t counter = 0;
      while (state.KeepRunning()) {
        std::string key =
            "key_" + std::to_string(t) + "_" + std::to_string(counter);
        std::string value = "value_" + std::to_string(counter);
        dbs[t]->Put(key, value);
        counter++;
        total_ops++;
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  state.SetItemsProcessed(total_ops.load());

  // Cleanup
  for (int i = 0; i < num_threads; ++i) {
    std::string thread_db_path = db_path + "_thread_" + std::to_string(i);
    CleanupDB(thread_db_path);
  }
}

// Register benchmarks
BENCHMARK(BM_RocksDB_SequentialWrite)
    ->Threads(1)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_RocksDB_SequentialRead)
    ->Arg(10000)
    ->Arg(100000)
    ->Threads(1)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_RocksDB_RandomRead)
    ->Arg(10000)
    ->Arg(100000)
    ->Threads(1)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_RocksDB_BatchWrite)->Threads(1)->Unit(benchmark::kMillisecond);

BENCHMARK(BM_RocksDB_CompressionWrite)
    ->Arg(rocksdb::kNoCompression)
    ->Arg(rocksdb::kSnappyCompression)
    ->Arg(rocksdb::kZSTD)
    ->Arg(rocksdb::kLZ4Compression)
    ->Threads(1)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_RocksDB_MultiThreadWrite)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Unit(benchmark::kMillisecond);

// Note: BENCHMARK_MAIN() is defined in main.cpp
