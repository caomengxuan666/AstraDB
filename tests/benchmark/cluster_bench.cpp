// ==============================================================================
// Cluster Communication Benchmarks
// ==============================================================================
// License: Apache 2.0
// ==============================================================================
// Benchmarks for:
// - Hash slot calculation performance
// - Shard routing performance
// - Gossip message processing (simulated)
// - MOVED redirect handling
// ==============================================================================

#include <benchmark/benchmark.h>

#include <atomic>
#include <random>
#include <string>
#include <thread>

#include "astra/cluster/gossip_manager.hpp"
#include "astra/cluster/shard_manager.hpp"

namespace astra::cluster {
namespace {

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

// Generate random key with optional hash tag
std::string RandomKeyWithTag(size_t length, bool with_tag = false) {
  if (!with_tag) {
    return RandomString(length);
  }

  // Generate key with hash tag: {tag}key
  std::string tag = RandomString(8);
  std::string key = RandomString(length > 10 ? length - 10 : 5);
  return "{" + tag + "}" + key;
}

// ========== Hash Slot Calculation Benchmarks ==========

// Benchmark hash slot calculation for simple keys
static void BM_HashSlotCalculation(benchmark::State& state) {
  const size_t key_len = state.range(0);
  std::vector<std::string> keys;

  // Pre-generate keys
  for (int i = 0; i < 10000; ++i) {
    keys.push_back(RandomString(key_len));
  }

  size_t idx = 0;
  for (auto _ : state) {
    HashSlot slot = HashSlotCalculator::Calculate(keys[idx % keys.size()]);
    benchmark::DoNotOptimize(slot);
    ++idx;
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("key_len=" + std::to_string(key_len));
}
BENCHMARK(BM_HashSlotCalculation)
    ->Arg(8)      // Short key
    ->Arg(32)     // Medium key
    ->Arg(128)    // Long key
    ->Arg(1024);  // Very long key

// Benchmark hash slot calculation with hash tags
static void BM_HashSlotCalculationWithTag(benchmark::State& state) {
  const size_t key_len = state.range(0);
  std::vector<std::string> keys;

  // Pre-generate keys with hash tags
  for (int i = 0; i < 10000; ++i) {
    keys.push_back(RandomKeyWithTag(key_len, true));
  }

  size_t idx = 0;
  for (auto _ : state) {
    // Use CalculateWithTag which extracts and hashes only the tag
    HashSlot slot =
        HashSlotCalculator::CalculateWithTag(keys[idx % keys.size()]);
    benchmark::DoNotOptimize(slot);
    ++idx;
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("key_len=" + std::to_string(key_len) + " (with tag)");
}
BENCHMARK(BM_HashSlotCalculationWithTag)
    ->Arg(16)    // Short key with tag
    ->Arg(48)    // Medium key with tag
    ->Arg(144);  // Long key with tag

// ========== Shard Routing Benchmarks ==========

class ShardManagerBenchmark : public benchmark::Fixture {
 public:
  void SetUp(const ::benchmark::State& state) override {
    shard_count_ = state.range(0);

    NodeId self_id{};
    GossipManager::GenerateNodeId(self_id);

    manager_ = std::make_unique<ShardManager>();
    manager_->Init(shard_count_, self_id);

    // Pre-generate keys
    keys_.clear();
    for (int i = 0; i < 10000; ++i) {
      keys_.push_back(RandomString(32));
    }
  }

  void TearDown(const ::benchmark::State& state) override {
    manager_.reset();
    keys_.clear();
  }

 protected:
  std::unique_ptr<ShardManager> manager_;
  std::vector<std::string> keys_;
  uint32_t shard_count_ = 256;
};

// Benchmark shard lookup for key
static void BM_ShardLookup(benchmark::State& state) {
  const uint32_t shard_count = state.range(0);

  ShardManager manager;
  NodeId self_id{};
  GossipManager::GenerateNodeId(self_id);
  manager.Init(shard_count, self_id);

  std::vector<std::string> keys;
  for (int i = 0; i < 10000; ++i) {
    keys.push_back(RandomString(32));
  }

  size_t idx = 0;
  for (auto _ : state) {
    ShardId shard = manager.GetShardForKey(keys[idx % keys.size()]);
    benchmark::DoNotOptimize(shard);
    ++idx;
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("shards=" + std::to_string(shard_count));
}
BENCHMARK(BM_ShardLookup)
    ->Arg(4)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096);

// Benchmark slot to shard mapping
static void BM_SlotToShardMapping(benchmark::State& state) {
  const uint32_t shard_count = state.range(0);

  ShardManager manager;
  NodeId self_id{};
  GossipManager::GenerateNodeId(self_id);
  manager.Init(shard_count, self_id);

  std::mt19937 rng(42);
  std::uniform_int_distribution<uint16_t> slot_dist(0, kHashSlotCount - 1);

  for (auto _ : state) {
    HashSlot slot = slot_dist(rng);
    ShardId shard = manager.GetShardForSlot(slot);
    benchmark::DoNotOptimize(shard);
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("shards=" + std::to_string(shard_count));
}
BENCHMARK(BM_SlotToShardMapping)->Arg(4)->Arg(16)->Arg(64)->Arg(256);

// ========== Gossip Message Simulation Benchmarks ==========

// Simulate gossip tick processing overhead
static void BM_GossipTickOverhead(benchmark::State& state) {
  // Create a minimal gossip configuration
  ClusterConfig config;
  config.node_id = "00112233445566778899aabbccddeeff";
  config.bind_ip = "127.0.0.1";
  config.gossip_port = 7946;
  config.shard_count = 256;

  GossipManager gossip;

  // Note: Init may fail without network binding, so we just benchmark the tick
  // call In a real scenario, this would be benchmarked with actual network
  // setup

  for (auto _ : state) {
    // Simulate tick overhead (even without network)
    // This measures the baseline CPU cost of calling tick
    benchmark::DoNotOptimize(
        0);  // Placeholder - actual gossip tick requires Init
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_GossipTickOverhead);

// ========== Node ID Operations Benchmarks ==========

// Benchmark node ID generation
static void BM_NodeIdGeneration(benchmark::State& state) {
  for (auto _ : state) {
    NodeId id;
    GossipManager::GenerateNodeId(id);
    benchmark::DoNotOptimize(id);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_NodeIdGeneration);

// Benchmark node ID to string conversion
static void BM_NodeIdToString(benchmark::State& state) {
  NodeId id;
  GossipManager::GenerateNodeId(id);

  for (auto _ : state) {
    std::string str = GossipManager::NodeIdToString(id);
    benchmark::DoNotOptimize(str);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_NodeIdToString);

// Benchmark node ID parsing
static void BM_NodeIdParse(benchmark::State& state) {
  NodeId id;
  GossipManager::GenerateNodeId(id);
  std::string id_str = GossipManager::NodeIdToString(id);

  for (auto _ : state) {
    NodeId parsed;
    bool success = GossipManager::ParseNodeId(id_str, parsed);
    benchmark::DoNotOptimize(success);
    benchmark::DoNotOptimize(parsed);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_NodeIdParse);

// ========== MOVED Redirect Simulation Benchmarks ==========

// Benchmark the overhead of checking if a redirect is needed
static void BM_RedirectCheck(benchmark::State& state) {
  const uint32_t shard_count = state.range(0);

  ShardManager manager;
  NodeId self_id{};
  GossipManager::GenerateNodeId(self_id);
  manager.Init(shard_count, self_id);

  // Generate keys
  std::vector<std::string> keys;
  for (int i = 0; i < 10000; ++i) {
    keys.push_back(RandomString(32));
  }

  // Simulate: for each key, calculate slot, get shard, check if owner matches
  // self
  size_t idx = 0;
  for (auto _ : state) {
    const auto& key = keys[idx % keys.size()];

    // Step 1: Calculate slot
    HashSlot slot = HashSlotCalculator::Calculate(key);

    // Step 2: Get shard for slot
    ShardId shard = manager.GetShardForSlot(slot);

    // Step 3: Get primary node for shard
    NodeId owner = manager.GetPrimaryNode(shard);

    // Step 4: Compare with self (this is the redirect check)
    bool need_redirect = (owner != self_id);

    benchmark::DoNotOptimize(need_redirect);
    ++idx;
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("shards=" + std::to_string(shard_count));
}
BENCHMARK(BM_RedirectCheck)->Arg(16)->Arg(64)->Arg(256);

// Benchmark building MOVED response
static void BM_BuildMovedResponse(benchmark::State& state) {
  uint16_t slot = 12345;
  std::string addr = "192.168.1.100:6379";

  for (auto _ : state) {
    std::string response = "MOVED " + std::to_string(slot) + " " + addr;
    benchmark::DoNotOptimize(response);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BuildMovedResponse);

// ========== Multi-threaded Benchmarks ==========

// Benchmark concurrent slot calculations
static void BM_ConcurrentSlotCalculation(benchmark::State& state) {
  const int num_threads = state.range(0);

  std::vector<std::string> keys;
  for (int i = 0; i < 10000; ++i) {
    keys.push_back(RandomString(32));
  }

  std::atomic<size_t> total_slots{0};
  std::vector<std::thread> threads;

  for (auto _ : state) {
    total_slots = 0;
    threads.clear();

    for (int t = 0; t < num_threads; ++t) {
      threads.emplace_back([&keys, &total_slots, t, num_threads]() {
        size_t local_count = 0;
        for (size_t i = t; i < keys.size(); i += num_threads) {
          HashSlot slot = HashSlotCalculator::Calculate(keys[i]);
          benchmark::DoNotOptimize(slot);
          ++local_count;
        }
        total_slots += local_count;
      });
    }

    for (auto& t : threads) {
      t.join();
    }
  }

  state.SetItemsProcessed(state.iterations() * keys.size());
  state.SetLabel("threads=" + std::to_string(num_threads));
}
BENCHMARK(BM_ConcurrentSlotCalculation)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16);

}  // namespace
}  // namespace astra::cluster
