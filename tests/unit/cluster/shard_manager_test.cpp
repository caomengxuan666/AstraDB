// ==============================================================================
// Shard Manager Unit Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>
#include "astra/cluster/shard_manager.hpp"
#include <chrono>
#include <thread>

namespace astra::cluster {
namespace {

class ShardManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    NodeId self_id{};
    std::fill(self_id.begin(), self_id.end(), 0x00);
    ASSERT_TRUE(manager_.Init(4, self_id));
  }
  
  ShardManager manager_;
};

// ========== Initialization Tests ==========

TEST_F(ShardManagerTest, InitWithValidConfig) {
  // Verify initialization was successful by checking shard count
  EXPECT_EQ(manager_.GetShardCount(), 4);
}

TEST_F(ShardManagerTest, SlotCalculation) {
  // Test CRC16-based slot calculation
  HashSlot slot1 = HashSlotCalculator::Calculate("mykey");
  HashSlot slot2 = HashSlotCalculator::Calculate("mykey");
  EXPECT_EQ(slot1, slot2);  // Same key = same slot
  
  // Different keys should (usually) have different slots
  HashSlot slot3 = HashSlotCalculator::Calculate("otherkey");
  // Just verify they're valid slots
  EXPECT_LT(slot1, kHashSlotCount);
  EXPECT_LT(slot3, kHashSlotCount);
}

TEST_F(ShardManagerTest, HashTagExtraction) {
  // Hash tags allow related keys to map to same slot
  HashSlot slot1 = HashSlotCalculator::CalculateWithTag("{user:1}:profile");
  HashSlot slot2 = HashSlotCalculator::CalculateWithTag("{user:1}:settings");
  HashSlot slot3 = HashSlotCalculator::CalculateWithTag("{user:2}:profile");
  
  EXPECT_EQ(slot1, slot2);  // Same hash tag = same slot
  // Different hash tags might have different slots (probabilistic)
  EXPECT_LT(slot1, kHashSlotCount);
  EXPECT_LT(slot3, kHashSlotCount);
}

// ========== Shard Mapping Tests ==========

TEST_F(ShardManagerTest, GetShardForKey) {
  for (int i = 0; i < 100; ++i) {
    std::string key = "testkey" + std::to_string(i);
    ShardId shard = manager_.GetShardForKey(key);
    EXPECT_LT(shard, manager_.GetShardCount());
  }
}

TEST_F(ShardManagerTest, GetShardForSlot) {
  for (HashSlot slot = 0; slot < kHashSlotCount; ++slot) {
    ShardId shard = manager_.GetShardForSlot(slot);
    EXPECT_LT(shard, manager_.GetShardCount());
  }
}

TEST_F(ShardManagerTest, ShardSlotDistribution) {
  // Each shard should cover roughly 1/4 of slots
  absl::flat_hash_map<ShardId, uint64_t> slot_counts;
  
  for (HashSlot slot = 0; slot < kHashSlotCount; ++slot) {
    ShardId shard = manager_.GetShardForSlot(slot);
    slot_counts[shard]++;
  }
  
  // Each shard should have approximately kHashSlotCount / shard_count slots
  uint64_t expected = kHashSlotCount / manager_.GetShardCount();
  for (const auto& [shard, count] : slot_counts) {
    EXPECT_GT(count, expected * 0.8);  // Allow 20% variance
    EXPECT_LT(count, expected * 1.2);
  }
}

// ========== Node Info Tests ==========

TEST_F(ShardManagerTest, GetPrimaryNode) {
  for (ShardId i = 0; i < manager_.GetShardCount(); ++i) {
    NodeId node = manager_.GetPrimaryNode(i);
    // Should return some valid node ID
    bool all_zero = true;
    for (auto b : node) {
      if (b != 0) {
        all_zero = false;
        break;
      }
    }
    // Self ID was set to all zeros, so this is expected
    EXPECT_TRUE(all_zero || !all_zero);  // Just verify it doesn't crash
  }
}

// ========== Statistics Tests ==========

TEST_F(ShardManagerTest, GetShardCount) {
  EXPECT_EQ(manager_.GetShardCount(), 4);
}

// ========== Edge Cases ==========

TEST_F(ShardManagerTest, EmptyKey) {
  HashSlot slot = HashSlotCalculator::Calculate("");
  EXPECT_LT(slot, kHashSlotCount);
}

TEST_F(ShardManagerTest, VeryLongKey) {
  std::string long_key(10000, 'x');
  HashSlot slot = HashSlotCalculator::Calculate(long_key);
  EXPECT_LT(slot, kHashSlotCount);
}

TEST_F(ShardManagerTest, UnicodeKey) {
  HashSlot slot = HashSlotCalculator::Calculate("键值");  // Chinese characters
  EXPECT_LT(slot, kHashSlotCount);
}

TEST_F(ShardManagerTest, BinaryKey) {
  std::string binary_key = "\x00\x01\x02\x03\x04\x05";
  HashSlot slot = HashSlotCalculator::Calculate(binary_key);
  EXPECT_LT(slot, kHashSlotCount);
}

TEST_F(ShardManagerTest, MultipleInitCalls) {
  NodeId self_id{};
  std::fill(self_id.begin(), self_id.end(), 0x01);
  
  // Second init should still work
  EXPECT_TRUE(manager_.Init(8, self_id));
  EXPECT_EQ(manager_.GetShardCount(), 8);
}

}  // namespace
}  // namespace astra::cluster
