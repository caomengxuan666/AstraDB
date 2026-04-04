// =============================================================================
// Memory Tracker Unit Tests
// =============================================================================

#include "astra/core/memory/memory_tracker.hpp"

#include <gtest/gtest.h>

namespace astra::core::memory {

class MemoryTrackerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MemoryTrackerConfig config;
    config.max_memory_limit = 1000000;  // 1MB
    config.eviction_threshold = 0.9;    // 90%
    config.eviction_samples = 5;
    config.enable_tracking = true;
    tracker_ = std::make_unique<MemoryTracker>(config);
  }

  std::unique_ptr<MemoryTracker> tracker_;
};

// Test initial state
TEST_F(MemoryTrackerTest, InitialState) {
  EXPECT_EQ(tracker_->GetCurrentMemory(), 0);
  EXPECT_EQ(tracker_->GetMaxMemory(), 1000000);
  EXPECT_EQ(tracker_->GetEvictionThreshold(), 0.9);
  EXPECT_EQ(tracker_->GetEvictionSamples(), 5);
  EXPECT_TRUE(tracker_->IsTrackingEnabled());
  EXPECT_FALSE(tracker_->ShouldEvict());
}

// Test AddMemory
TEST_F(MemoryTrackerTest, AddMemory) {
  tracker_->AddMemory(1000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 1000);

  tracker_->AddMemory(2000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 3000);
}

// Test SubtractMemory
TEST_F(MemoryTrackerTest, SubtractMemory) {
  tracker_->AddMemory(5000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 5000);

  tracker_->SubtractMemory(2000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 3000);

  // Subtract more than available
  tracker_->SubtractMemory(5000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 0);
}

// Test UpdateMemory - replacement
TEST_F(MemoryTrackerTest, UpdateMemory_Replacement) {
  tracker_->UpdateMemory(0, 1000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 1000);

  // Replace 1000 bytes with 2000 bytes
  tracker_->UpdateMemory(1000, 2000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 2000);

  // Replace 2000 bytes with 1000 bytes
  tracker_->UpdateMemory(2000, 1000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 1000);
}

// Test UpdateMemory - deletion
TEST_F(MemoryTrackerTest, UpdateMemory_Deletion) {
  tracker_->UpdateMemory(0, 1000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 1000);

  // Delete 1000 bytes
  tracker_->UpdateMemory(1000, 0);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 0);
}

// Test ShouldEvict
TEST_F(MemoryTrackerTest, ShouldEvict) {
  // Set an active eviction policy
  tracker_->SetEvictionPolicy(EvictionPolicy::kAllKeysLRU);

  // Memory is just below threshold (89% of max = 890000 bytes)
  for (int i = 0; i < 890; ++i) {
    tracker_->AddMemory(1000);
  }
  EXPECT_EQ(tracker_->GetCurrentMemory(), 890000);
  EXPECT_FALSE(tracker_->ShouldEvict());  // Below threshold

  // Add 10001 bytes to exceed threshold (90% = 900000 bytes)
  tracker_->AddMemory(10001);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 900001);
  EXPECT_TRUE(tracker_->ShouldEvict());  // At or above threshold

  // Add memory to exactly at threshold
  tracker_->Reset();
  for (int i = 0; i < 900; ++i) {
    tracker_->AddMemory(1000);
  }
  EXPECT_EQ(tracker_->GetCurrentMemory(), 900000);
  EXPECT_TRUE(tracker_->ShouldEvict());  // At threshold
}

// Test SetMaxMemory
TEST_F(MemoryTrackerTest, SetMaxMemory) {
  tracker_->SetEvictionPolicy(EvictionPolicy::kAllKeysLRU);
  tracker_->AddMemory(540000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 540000);

  // Set max to 600000, threshold is 0.9 * 600000 = 540000
  // At threshold, should evict
  tracker_->SetMaxMemory(600000);
  EXPECT_EQ(tracker_->GetMaxMemory(), 600000);
  EXPECT_TRUE(tracker_->ShouldEvict());  // 540000 == 540000 (at threshold)

  // Set max to 1000000, threshold is 0.9 * 1000000 = 900000
  // Below threshold, no eviction needed
  tracker_->SetMaxMemory(1000000);
  EXPECT_EQ(tracker_->GetMaxMemory(), 1000000);
  EXPECT_FALSE(tracker_->ShouldEvict());  // 540000 < 900000
}

// Test SetEvictionThreshold
TEST_F(MemoryTrackerTest, SetEvictionThreshold) {
  tracker_->SetEvictionPolicy(EvictionPolicy::kAllKeysLRU);
  tracker_->AddMemory(850000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 850000);

  // Default threshold is 0.9, so 850000 < 900000, no eviction needed
  EXPECT_FALSE(tracker_->ShouldEvict());

  // Lower threshold to 0.8, now 850000 > 800000, eviction needed
  tracker_->SetEvictionThreshold(0.8);
  EXPECT_EQ(tracker_->GetEvictionThreshold(), 0.8);
  EXPECT_TRUE(tracker_->ShouldEvict());

  // Raise threshold to 1.0, no eviction needed
  tracker_->SetEvictionThreshold(1.0);
  EXPECT_EQ(tracker_->GetEvictionThreshold(), 1.0);
  EXPECT_FALSE(tracker_->ShouldEvict());
}

// Test SetEvictionSamples
TEST_F(MemoryTrackerTest, SetEvictionSamples) {
  EXPECT_EQ(tracker_->GetEvictionSamples(), 5);

  tracker_->SetEvictionSamples(10);
  EXPECT_EQ(tracker_->GetEvictionSamples(), 10);

  tracker_->SetEvictionSamples(3);
  EXPECT_EQ(tracker_->GetEvictionSamples(), 3);
}

// Test GetMemoryUsagePercentage
TEST_F(MemoryTrackerTest, GetMemoryUsagePercentage) {
  EXPECT_DOUBLE_EQ(tracker_->GetMemoryUsagePercentage(), 0.0);

  tracker_->AddMemory(500000);
  EXPECT_DOUBLE_EQ(tracker_->GetMemoryUsagePercentage(), 0.5);

  tracker_->AddMemory(500000);
  EXPECT_DOUBLE_EQ(tracker_->GetMemoryUsagePercentage(), 1.0);
}

// Test GetFreeMemory
TEST_F(MemoryTrackerTest, GetFreeMemory) {
  EXPECT_EQ(tracker_->GetFreeMemory(), 1000000);

  tracker_->AddMemory(300000);
  EXPECT_EQ(tracker_->GetFreeMemory(), 700000);
}

// Test SetTrackingEnabled
TEST_F(MemoryTrackerTest, SetTrackingEnabled) {
  EXPECT_TRUE(tracker_->IsTrackingEnabled());

  tracker_->SetTrackingEnabled(false);
  EXPECT_FALSE(tracker_->IsTrackingEnabled());

  tracker_->AddMemory(1000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 0);  // No change when disabled

  tracker_->SetTrackingEnabled(true);
  EXPECT_TRUE(tracker_->IsTrackingEnabled());

  tracker_->AddMemory(1000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 1000);  // Now tracking works
}

// Test tracking disabled behavior
TEST_F(MemoryTrackerTest, TrackingDisabled_Behavior) {
  tracker_->SetTrackingEnabled(false);
  EXPECT_FALSE(tracker_->IsTrackingEnabled());

  // All operations should be no-ops when tracking is disabled
  tracker_->AddMemory(1000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 0);

  EXPECT_FALSE(tracker_->ShouldEvict());
}

// Test Reset
TEST_F(MemoryTrackerTest, Reset) {
  tracker_->AddMemory(500000);
  EXPECT_EQ(tracker_->GetCurrentMemory(), 500000);

  tracker_->Reset();
  EXPECT_EQ(tracker_->GetCurrentMemory(), 0);
}

// Test GetMemoryUsageHuman
TEST_F(MemoryTrackerTest, GetMemoryUsageHuman) {
  tracker_->AddMemory(10000);
  std::string human = tracker_->GetMemoryUsageHuman();
  // Format: "<value> <unit>" (e.g., "9.77 KB")
  EXPECT_FALSE(human.empty());
  EXPECT_TRUE(human.find("KB") != std::string::npos ||
              human.find("MB") != std::string::npos);
}

// Test GetMaxMemoryHuman
TEST_F(MemoryTrackerTest, GetMaxMemoryHuman) {
  std::string human = tracker_->GetMaxMemoryHuman();
  // Format: "<value> <unit>" (e.g., "976.56 KB")
  EXPECT_FALSE(human.empty());
  EXPECT_TRUE(human.find("KB") != std::string::npos ||
              human.find("MB") != std::string::npos);
}

// Test ShouldEvictVolatileOnly
TEST_F(MemoryTrackerTest, ShouldEvictVolatileOnly) {
  tracker_->SetEvictionPolicy(EvictionPolicy::kNoEviction);
  EXPECT_FALSE(tracker_->ShouldEvictVolatileOnly());

  tracker_->SetEvictionPolicy(EvictionPolicy::kAllKeysLRU);
  EXPECT_FALSE(tracker_->ShouldEvictVolatileOnly());

  tracker_->SetEvictionPolicy(EvictionPolicy::kVolatileLRU);
  EXPECT_TRUE(tracker_->ShouldEvictVolatileOnly());

  tracker_->SetEvictionPolicy(EvictionPolicy::kVolatileLFU);
  EXPECT_TRUE(tracker_->ShouldEvictVolatileOnly());

  tracker_->SetEvictionPolicy(EvictionPolicy::kVolatileRandom);
  EXPECT_TRUE(tracker_->ShouldEvictVolatileOnly());

  tracker_->SetEvictionPolicy(EvictionPolicy::kVolatileTTL);
  EXPECT_TRUE(tracker_->ShouldEvictVolatileOnly());
}

// Test IsMemoryFull
TEST_F(MemoryTrackerTest, IsMemoryFull) {
  EXPECT_FALSE(tracker_->IsMemoryFull());

  tracker_->AddMemory(999999);
  EXPECT_FALSE(tracker_->IsMemoryFull());

  tracker_->AddMemory(1);
  EXPECT_TRUE(tracker_->IsMemoryFull());
}

// Test GetEvictionPolicy
TEST_F(MemoryTrackerTest, GetEvictionPolicy) {
  EXPECT_EQ(tracker_->GetEvictionPolicy(), EvictionPolicy::kNoEviction);

  tracker_->SetEvictionPolicy(EvictionPolicy::kAllKeysLRU);
  EXPECT_EQ(tracker_->GetEvictionPolicy(), EvictionPolicy::kAllKeysLRU);

  tracker_->SetEvictionPolicy(EvictionPolicy::kVolatileLFU);
  EXPECT_EQ(tracker_->GetEvictionPolicy(), EvictionPolicy::kVolatileLFU);
}

// Test GetConfig
TEST_F(MemoryTrackerTest, GetConfig) {
  const auto& config = tracker_->GetConfig();
  EXPECT_EQ(config.max_memory_limit, 1000000);
  EXPECT_EQ(config.eviction_threshold, 0.9);
  EXPECT_EQ(config.eviction_samples, 5);
  EXPECT_TRUE(config.enable_tracking);
}

}  // namespace astra::core::memory
