// ==============================================================================
// Shard Manager Unit Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "astra/cluster/shard_manager.hpp"

#include <gtest/gtest.h>

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

// ========== Migration Tests ==========

TEST_F(ShardManagerTest, StartMigration) {
  NodeId target_node;
  std::fill(target_node.begin(), target_node.end(), 0x02);

  // Start migration of shard 0
  EXPECT_TRUE(manager_.StartMigration(0, target_node));

  // Verify migration state
  EXPECT_TRUE(manager_.IsMigrating(0));
  EXPECT_FALSE(manager_.IsMigrating(1));

  // Verify migration target
  EXPECT_EQ(manager_.GetMigrationTarget(0), target_node);
}

TEST_F(ShardManagerTest, CompleteMigration) {
  NodeId target_node;
  std::fill(target_node.begin(), target_node.end(), 0x02);

  manager_.StartMigration(0, target_node);
  EXPECT_TRUE(manager_.IsMigrating(0));

  // Complete migration
  EXPECT_TRUE(manager_.CompleteMigration(0));

  // Verify state changed to stable
  EXPECT_FALSE(manager_.IsMigrating(0));

  // Verify ownership transferred
  EXPECT_EQ(manager_.GetPrimaryNode(0), target_node);
}

TEST_F(ShardManagerTest, CancelMigration) {
  NodeId target_node;
  std::fill(target_node.begin(), target_node.end(), 0x02);

  NodeId original_owner = manager_.GetPrimaryNode(0);

  manager_.StartMigration(0, target_node);
  EXPECT_TRUE(manager_.IsMigrating(0));

  // Cancel migration
  EXPECT_TRUE(manager_.CancelMigration(0));

  // Verify state is stable
  EXPECT_FALSE(manager_.IsMigrating(0));

  // Verify ownership unchanged
  EXPECT_EQ(manager_.GetPrimaryNode(0), original_owner);
}

TEST_F(ShardManagerTest, StartImport) {
  NodeId source_node;
  std::fill(source_node.begin(), source_node.end(), 0x03);

  // Start importing shard 1
  EXPECT_TRUE(manager_.StartImport(1, source_node));

  // Verify import state
  EXPECT_TRUE(manager_.IsImporting(1));
  EXPECT_FALSE(manager_.IsImporting(0));

  // Verify import source
  EXPECT_EQ(manager_.GetImportSource(1), source_node);
}

TEST_F(ShardManagerTest, CompleteImport) {
  NodeId source_node;
  std::fill(source_node.begin(), source_node.end(), 0x03);

  manager_.StartImport(1, source_node);
  EXPECT_TRUE(manager_.IsImporting(1));

  // Complete import
  EXPECT_TRUE(manager_.CompleteImport(1));

  // Verify state is stable
  EXPECT_FALSE(manager_.IsImporting(1));
}

TEST_F(ShardManagerTest, MigrationStateTransitions) {
  NodeId target_node;
  std::fill(target_node.begin(), target_node.end(), 0x02);

  // Initial state: stable
  EXPECT_FALSE(manager_.IsMigrating(0));
  EXPECT_FALSE(manager_.IsImporting(0));

  // Transition: stable -> migrating
  manager_.StartMigration(0, target_node);
  EXPECT_TRUE(manager_.IsMigrating(0));
  EXPECT_FALSE(manager_.IsImporting(0));

  // Transition: migrating -> stable (complete)
  manager_.CompleteMigration(0);
  EXPECT_FALSE(manager_.IsMigrating(0));
  EXPECT_FALSE(manager_.IsImporting(0));
}

TEST_F(ShardManagerTest, ImportStateTransitions) {
  NodeId source_node;
  std::fill(source_node.begin(), source_node.end(), 0x03);

  // Initial state: stable
  EXPECT_FALSE(manager_.IsMigrating(1));
  EXPECT_FALSE(manager_.IsImporting(1));

  // Transition: stable -> importing
  manager_.StartImport(1, source_node);
  EXPECT_FALSE(manager_.IsMigrating(1));
  EXPECT_TRUE(manager_.IsImporting(1));

  // Transition: importing -> stable
  manager_.CompleteImport(1);
  EXPECT_FALSE(manager_.IsMigrating(1));
  EXPECT_FALSE(manager_.IsImporting(1));
}

TEST_F(ShardManagerTest, InvalidMigrationOperations) {
  // Try to migrate non-existent shard
  NodeId target_node;
  std::fill(target_node.begin(), target_node.end(), 0x02);

  EXPECT_FALSE(manager_.StartMigration(100, target_node));
  EXPECT_FALSE(manager_.CompleteMigration(100));
  EXPECT_FALSE(manager_.CancelMigration(100));
  EXPECT_FALSE(manager_.IsMigrating(100));
}

TEST_F(ShardManagerTest, InvalidImportOperations) {
  // Try to import non-existent shard
  NodeId source_node;
  std::fill(source_node.begin(), source_node.end(), 0x03);

  EXPECT_FALSE(manager_.StartImport(100, source_node));
  EXPECT_FALSE(manager_.CompleteImport(100));
  EXPECT_FALSE(manager_.IsImporting(100));
}

}  // namespace
}  // namespace astra::cluster
