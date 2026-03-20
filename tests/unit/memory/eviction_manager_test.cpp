// =============================================================================
// Eviction Manager Unit Tests
// =============================================================================

#include <gtest/gtest.h>
#include "astra/core/memory/eviction_manager.hpp"
#include "astra/core/memory/memory_tracker.hpp"
#include "astra/storage/key_metadata.hpp"

namespace astra::core::memory {

class EvictionManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MemoryTrackerConfig config;
    config.max_memory_limit = 100000;  // 100KB
    config.eviction_threshold = 0.8;   // 80%
    config.eviction_samples = 5;
    config.enable_tracking = true;
    config.eviction_policy = EvictionPolicy::kNoEviction;
    memory_tracker_ = std::make_unique<MemoryTracker>(config);

    metadata_manager_ = std::make_unique<astra::storage::KeyMetadataManager>();

    eviction_manager_ = std::make_unique<EvictionManager>(
      memory_tracker_.get(),
      metadata_manager_.get());

    // Set up eviction callback
    eviction_manager_->SetEvictionCallback(
      [this](const std::string& key, astra::storage::KeyType type) -> bool {
        evicted_keys_.push_back(key);
        // Simulate freeing memory when key is evicted
        memory_tracker_->SubtractMemory(10000);  // Free 10KB
        // Remove the key from metadata manager
        metadata_manager_->UnregisterKey(key);
        return true;
      });
  }

  void TearDown() override {
    evicted_keys_.clear();
  }

  // Helper function to add test keys
  void AddTestKeys(const std::vector<std::string>& keys) {
    for (const auto& key : keys) {
      metadata_manager_->RegisterKey(key, astra::storage::KeyType::kString);
    }
  }

  std::unique_ptr<MemoryTracker> memory_tracker_;
  std::unique_ptr<astra::storage::KeyMetadataManager> metadata_manager_;
  std::unique_ptr<EvictionManager> eviction_manager_;
  std::vector<std::string> evicted_keys_;
};

// Test initial state
TEST_F(EvictionManagerTest, InitialState) {
  EXPECT_EQ(memory_tracker_->GetEvictionPolicy(), EvictionPolicy::kNoEviction);
  EXPECT_TRUE(memory_tracker_->IsTrackingEnabled());
  EXPECT_EQ(evicted_keys_.size(), 0);
}

// Test SetEvictionPolicy
TEST_F(EvictionManagerTest, SetEvictionPolicy) {
  memory_tracker_->SetEvictionPolicy(EvictionPolicy::kAllKeysLRU);
  EXPECT_EQ(memory_tracker_->GetEvictionPolicy(), EvictionPolicy::kAllKeysLRU);

  memory_tracker_->SetEvictionPolicy(EvictionPolicy::kVolatileLFU);
  EXPECT_EQ(memory_tracker_->GetEvictionPolicy(), EvictionPolicy::kVolatileLFU);

  memory_tracker_->SetEvictionPolicy(EvictionPolicy::kNoEviction);
  EXPECT_EQ(memory_tracker_->GetEvictionPolicy(), EvictionPolicy::kNoEviction);
}

// Test CheckAndEvict - no eviction needed
TEST_F(EvictionManagerTest, CheckAndEvict_NoEvictionNeeded) {
  memory_tracker_->SetEvictionPolicy(EvictionPolicy::kAllKeysLRU);

  // Memory is below threshold
  memory_tracker_->AddMemory(50000);  // 50KB < 80KB threshold

  EXPECT_EQ(eviction_manager_->CheckAndEvict(), 0);
  EXPECT_EQ(evicted_keys_.size(), 0);
}

// Test CheckAndEvict - with noeviction policy
TEST_F(EvictionManagerTest, CheckAndEvict_NoEvictionPolicy) {
  // Policy is kNoEviction by default

  // Memory exceeds threshold
  memory_tracker_->AddMemory(90000);  // 90KB > 80KB threshold

  EXPECT_EQ(eviction_manager_->CheckAndEvict(), 0);
  EXPECT_EQ(evicted_keys_.size(), 0);
}

// Test CheckAndEvict - with allkeys-lru policy
TEST_F(EvictionManagerTest, CheckAndEvict_AllKeysLRU) {
  memory_tracker_->SetEvictionPolicy(EvictionPolicy::kAllKeysLRU);

  // Memory exceeds threshold
  memory_tracker_->AddMemory(90000);  // 90KB > 80KB threshold

  // Add test keys
  AddTestKeys({"key1", "key2", "key3"});

  size_t evicted = eviction_manager_->CheckAndEvict();
  EXPECT_GT(evicted, 0);  // At least one key should be evicted
  EXPECT_GT(evicted_keys_.size(), 0);
}

// Test CheckAndEvict - with volatile-lru policy
TEST_F(EvictionManagerTest, CheckAndEvict_VolatileLRU) {
  memory_tracker_->SetEvictionPolicy(EvictionPolicy::kVolatileLRU);

  // Memory exceeds threshold
  memory_tracker_->AddMemory(90000);  // 90KB > 80KB threshold

  // Add test keys, some with TTL
  AddTestKeys({"key1", "key2", "key3"});
  metadata_manager_->SetExpireSeconds("key1", 300);  // key1 has TTL
  metadata_manager_->SetExpireSeconds("key3", 300);  // key3 has TTL

  size_t evicted = eviction_manager_->CheckAndEvict();
  EXPECT_GT(evicted, 0);  // At least one key should be evicted
  EXPECT_GT(evicted_keys_.size(), 0);
}

// Test CheckAndEvict - with allkeys-lfu policy
TEST_F(EvictionManagerTest, CheckAndEvict_AllKeysLFU) {
  memory_tracker_->SetEvictionPolicy(EvictionPolicy::kAllKeysLFU);

  // Memory exceeds threshold
  memory_tracker_->AddMemory(90000);  // 90KB > 80KB threshold

  // Add test keys
  AddTestKeys({"key1", "key2", "key3"});

  size_t evicted = eviction_manager_->CheckAndEvict();
  EXPECT_GT(evicted, 0);  // At least one key should be evicted
  EXPECT_GT(evicted_keys_.size(), 0);
}

// Test CheckAndEvict - with allkeys-random policy
TEST_F(EvictionManagerTest, CheckAndEvict_AllKeysRandom) {
  memory_tracker_->SetEvictionPolicy(EvictionPolicy::kAllKeysRandom);

  // Memory exceeds threshold
  memory_tracker_->AddMemory(90000);  // 90KB > 80KB threshold

  // Add test keys
  AddTestKeys({"key1", "key2", "key3"});

  size_t evicted = eviction_manager_->CheckAndEvict();
  EXPECT_GT(evicted, 0);  // At least one key should be evicted
  EXPECT_GT(evicted_keys_.size(), 0);
}

// Test CheckAndEvict - with volatile-ttl policy
TEST_F(EvictionManagerTest, CheckAndEvict_VolatileTTL) {
  memory_tracker_->SetEvictionPolicy(EvictionPolicy::kVolatileTTL);

  // Memory exceeds threshold
  memory_tracker_->AddMemory(90000);  // 90KB > 80KB threshold

  // Add test keys, some with TTL
  AddTestKeys({"key1", "key2", "key3"});
  metadata_manager_->SetExpireSeconds("key1", 1);   // key1 expires in 1 second
  metadata_manager_->SetExpireSeconds("key3", 10);  // key3 expires in 10 seconds

  size_t evicted = eviction_manager_->CheckAndEvict();
  EXPECT_GT(evicted, 0);  // At least one key should be evicted
  EXPECT_GT(evicted_keys_.size(), 0);
}

// Test CheckAndEvict - tracking disabled
TEST_F(EvictionManagerTest, CheckAndEvict_TrackingDisabled) {
  memory_tracker_->SetEvictionPolicy(EvictionPolicy::kAllKeysLRU);
  memory_tracker_->SetTrackingEnabled(false);

  // Memory exceeds threshold (but tracking is disabled)
  memory_tracker_->AddMemory(90000);  // 90KB > 80KB threshold

  EXPECT_EQ(eviction_manager_->CheckAndEvict(), 0);
  EXPECT_EQ(evicted_keys_.size(), 0);
}

// Test GetStats
TEST_F(EvictionManagerTest, GetStats) {
  auto stats = eviction_manager_->GetStats();
  EXPECT_EQ(stats.total_evicted, 0);
  EXPECT_EQ(stats.lru_evicted, 0);
  EXPECT_EQ(stats.lfu_evicted, 0);
  EXPECT_EQ(stats.random_evicted, 0);
  EXPECT_EQ(stats.ttl_evicted, 0);
}



// Test multiple evictions
TEST_F(EvictionManagerTest, MultipleEvictions) {
  memory_tracker_->SetEvictionPolicy(EvictionPolicy::kAllKeysRandom);

  // Memory exceeds threshold significantly
  memory_tracker_->AddMemory(95000);  // 95KB > 80KB threshold

  // Add test keys
  AddTestKeys({"key1", "key2", "key3", "key4", "key5"});

  // First eviction
  size_t evicted = eviction_manager_->CheckAndEvict();
  EXPECT_GT(evicted, 0);
  EXPECT_GT(evicted_keys_.size(), 0);

  // Memory should now be below threshold (85KB after freeing 10KB)
  EXPECT_FALSE(memory_tracker_->ShouldEvict());
}

// Test eviction with no keys
TEST_F(EvictionManagerTest, EvictionWithNoKeys) {
  memory_tracker_->SetEvictionPolicy(EvictionPolicy::kAllKeysLRU);

  // Memory exceeds threshold
  memory_tracker_->AddMemory(90000);  // 90KB > 80KB threshold

  // No keys available

  size_t evicted = eviction_manager_->CheckAndEvict();
  EXPECT_EQ(evicted, 0);
  EXPECT_EQ(evicted_keys_.size(), 0);
}

}  // namespace astra::core::memory