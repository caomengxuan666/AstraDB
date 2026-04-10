// ==============================================================================
// Multi-Database Performance Benchmarks
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <benchmark/benchmark.h>

#include <random>
#include <thread>
#include <vector>

#include "astra/commands/database.hpp"
#include "astra/commands/redis/admin_commands.hpp"
#include "astra/commands/redis/string_commands.hpp"
#include "astra/protocol/resp/resp_types.hpp"

namespace astra::commands {
namespace {

// Generate random key (reserved for future use)
[[maybe_unused]] static std::string RandomKey(size_t length = 16) {
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

// Generate random value
std::string RandomValue(size_t length = 100) {
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

// Benchmark: Database switching overhead (SELECT command)
static void BM_SelectCommand(benchmark::State& state) {
  DatabaseManager db_manager(16);

  for (auto _ : state) {
    for (int i = 0; i < 16; ++i) {
      auto* db = db_manager.GetDatabase(i);
      benchmark::DoNotOptimize(db);
    }
  }
  state.SetItemsProcessed(state.iterations() * 16);
}

// Benchmark: Write operations across multiple databases
static void BM_MultiDBWrites(benchmark::State& state) {
  DatabaseManager db_manager(16);
  const int num_dbs = static_cast<int>(state.range(0));
  const int keys_per_db = static_cast<int>(state.range(1));

  std::vector<std::string> keys;
  std::vector<std::string> values;
  keys.reserve(keys_per_db);
  values.reserve(keys_per_db);

  for (int i = 0; i < keys_per_db; ++i) {
    keys.push_back("key_" + std::to_string(i));
    values.push_back(RandomValue(100));
  }

  for (auto _ : state) {
    for (int db_idx = 0; db_idx < num_dbs; ++db_idx) {
      auto* db = db_manager.GetDatabase(db_idx);
      for (int i = 0; i < keys_per_db; ++i) {
        db->Set(keys[i], StringValue(values[i]));
      }
    }
  }

  state.SetItemsProcessed(state.iterations() * num_dbs * keys_per_db);
}

// Benchmark: Read operations across multiple databases
static void BM_MultiDBReads(benchmark::State& state) {
  DatabaseManager db_manager(16);
  const int num_dbs = static_cast<int>(state.range(0));
  const int keys_per_db = static_cast<int>(state.range(1));

  // Pre-populate databases
  for (int db_idx = 0; db_idx < num_dbs; ++db_idx) {
    auto* db = db_manager.GetDatabase(db_idx);
    for (int i = 0; i < keys_per_db; ++i) {
      db->Set("key_" + std::to_string(i),
              StringValue("value_" + std::to_string(db_idx) + "_" +
                          std::to_string(i)));
    }
  }

  for (auto _ : state) {
    for (int db_idx = 0; db_idx < num_dbs; ++db_idx) {
      auto* db = db_manager.GetDatabase(db_idx);
      for (int i = 0; i < keys_per_db; ++i) {
        auto val = db->Get("key_" + std::to_string(i));
        benchmark::DoNotOptimize(val);
      }
    }
  }

  state.SetItemsProcessed(state.iterations() * num_dbs * keys_per_db);
}

// Benchmark: FLUSHDB command (single database)
static void BM_FlushDBCommand(benchmark::State& state) {
  const int num_keys = static_cast<int>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    DatabaseManager db_manager(16);
    auto* db = db_manager.GetDatabase(0);

    // Populate database
    for (int i = 0; i < num_keys; ++i) {
      db->Set("key_" + std::to_string(i), StringValue(RandomValue(100)));
    }
    state.ResumeTiming();

    // Clear database
    db->Clear();
  }

  state.SetItemsProcessed(state.iterations() * num_keys);
}

// Benchmark: FLUSHALL command (all databases)
static void BM_FlushAllCommand(benchmark::State& state) {
  const int num_dbs = 16;
  const int keys_per_db = static_cast<int>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    DatabaseManager db_manager(16);

    // Populate all databases
    for (int db_idx = 0; db_idx < num_dbs; ++db_idx) {
      auto* db = db_manager.GetDatabase(db_idx);
      for (int i = 0; i < keys_per_db; ++i) {
        db->Set("key_" + std::to_string(i), StringValue(RandomValue(100)));
      }
    }
    state.ResumeTiming();

    // Clear all databases
    for (int db_idx = 0; db_idx < num_dbs; ++db_idx) {
      auto* db = db_manager.GetDatabase(db_idx);
      db->Clear();
    }
  }

  state.SetItemsProcessed(state.iterations() * num_dbs * keys_per_db);
}

// Benchmark: DBSIZE command across multiple databases
static void BM_DBSizeCommand(benchmark::State& state) {
  DatabaseManager db_manager(16);
  const int num_dbs = static_cast<int>(state.range(0));
  const int keys_per_db = static_cast<int>(state.range(1));

  // Pre-populate databases
  for (int db_idx = 0; db_idx < num_dbs; ++db_idx) {
    auto* db = db_manager.GetDatabase(db_idx);
    for (int i = 0; i < keys_per_db; ++i) {
      db->Set("key_" + std::to_string(i), StringValue(RandomValue(100)));
    }
  }

  for (auto _ : state) {
    for (int db_idx = 0; db_idx < num_dbs; ++db_idx) {
      auto* db = db_manager.GetDatabase(db_idx);
      size_t size = db->DbSize();
      benchmark::DoNotOptimize(size);
    }
  }

  state.SetItemsProcessed(state.iterations() * num_dbs);
}

// Benchmark: Mixed operations across databases
static void BM_MixedOperations(benchmark::State& state) {
  DatabaseManager db_manager(16);
  const int num_ops = static_cast<int>(state.range(0));

  std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> db_dist(0, 15);

  std::vector<std::string> keys;
  for (int i = 0; i < 1000; ++i) {
    keys.push_back("key_" + std::to_string(i));
  }

  for (auto _ : state) {
    int db_idx = db_dist(rng);
    auto* db = db_manager.GetDatabase(db_idx);
    int op_type = rng() % 4;
    int key_idx = rng() % keys.size();

    switch (op_type) {
      case 0:  // SET
        db->Set(keys[key_idx], StringValue(RandomValue(100)));
        break;
      case 1:  // GET
        db->Get(keys[key_idx]);
        break;
      case 2:  // DEL
        db->Del(keys[key_idx]);
        break;
      case 3:  // DBSIZE
        db->DbSize();
        break;
    }
  }

  state.SetItemsProcessed(state.iterations() * num_ops);
}

// Benchmark: Concurrent access to multiple databases
static void BM_ConcurrentMultiDBAccess(benchmark::State& state) {
  DatabaseManager db_manager(16);
  const int num_threads = static_cast<int>(state.range(0));
  const int ops_per_thread = 1000;

  std::vector<std::thread> threads;
  std::atomic<int> barrier{0};

  for (auto _ : state) {
    threads.clear();
    barrier.store(0);

    for (int t = 0; t < num_threads; ++t) {
      threads.emplace_back([&, t]() {
        // Wait for all threads to start
        barrier.fetch_add(1);
        while (barrier.load() < num_threads) {
        }

        std::mt19937 rng(t);
        std::uniform_int_distribution<int> db_dist(0, 15);

        for (int i = 0; i < ops_per_thread; ++i) {
          int db_idx = db_dist(rng);
          auto* db = db_manager.GetDatabase(db_idx);
          db->Set("thread_" + std::to_string(t) + "_key_" + std::to_string(i),
                  StringValue("value_" + std::to_string(i)));
          db->Get("thread_" + std::to_string(t) + "_key_" + std::to_string(i));
        }
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }
  }

  state.SetItemsProcessed(state.iterations() * num_threads * ops_per_thread *
                          2);
}

// Register benchmarks
BENCHMARK(BM_SelectCommand);
BENCHMARK(BM_MultiDBWrites)->ArgsProduct({{1, 4, 8, 16}, {10, 100, 1000}});
BENCHMARK(BM_MultiDBReads)->ArgsProduct({{1, 4, 8, 16}, {10, 100, 1000}});
BENCHMARK(BM_FlushDBCommand)->Range(10, 10000);
BENCHMARK(BM_FlushAllCommand)->Range(10, 10000);
BENCHMARK(BM_DBSizeCommand)->ArgsProduct({{1, 4, 8, 16}, {10, 100, 1000}});
BENCHMARK(BM_MixedOperations)->Range(100, 100000);
BENCHMARK(BM_ConcurrentMultiDBAccess)->Range(1, 16);

}  // namespace
}  // namespace astra::commands
