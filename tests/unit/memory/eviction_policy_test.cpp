// =============================================================================
// Eviction Policy Unit Tests
// =============================================================================

#include "astra/core/memory/eviction_policy.hpp"

#include <gtest/gtest.h>

namespace astra::core::memory {

// Test EvictionPolicyFromString
TEST(EvictionPolicyTest, StringToEvictionPolicy) {
  EXPECT_EQ(StringToEvictionPolicy("noeviction"), EvictionPolicy::kNoEviction);
  EXPECT_EQ(StringToEvictionPolicy("allkeys-lru"), EvictionPolicy::kAllKeysLRU);
  EXPECT_EQ(StringToEvictionPolicy("volatile-lru"),
            EvictionPolicy::kVolatileLRU);
  EXPECT_EQ(StringToEvictionPolicy("allkeys-lfu"), EvictionPolicy::kAllKeysLFU);
  EXPECT_EQ(StringToEvictionPolicy("volatile-lfu"),
            EvictionPolicy::kVolatileLFU);
  EXPECT_EQ(StringToEvictionPolicy("allkeys-random"),
            EvictionPolicy::kAllKeysRandom);
  EXPECT_EQ(StringToEvictionPolicy("volatile-random"),
            EvictionPolicy::kVolatileRandom);
  EXPECT_EQ(StringToEvictionPolicy("volatile-ttl"),
            EvictionPolicy::kVolatileTTL);
}

TEST(EvictionPolicyTest, StringToEvictionPolicy_Invalid) {
  EXPECT_EQ(StringToEvictionPolicy("invalid"), EvictionPolicy::kNoEviction);
  EXPECT_EQ(StringToEvictionPolicy(""), EvictionPolicy::kNoEviction);
}

// Test EvictionPolicyToString
TEST(EvictionPolicyTest, EvictionPolicyToString) {
  EXPECT_EQ(EvictionPolicyToString(EvictionPolicy::kNoEviction), "noeviction");
  EXPECT_EQ(EvictionPolicyToString(EvictionPolicy::kAllKeysLRU), "allkeys-lru");
  EXPECT_EQ(EvictionPolicyToString(EvictionPolicy::kVolatileLRU),
            "volatile-lru");
  EXPECT_EQ(EvictionPolicyToString(EvictionPolicy::kAllKeysLFU), "allkeys-lfu");
  EXPECT_EQ(EvictionPolicyToString(EvictionPolicy::kVolatileLFU),
            "volatile-lfu");
  EXPECT_EQ(EvictionPolicyToString(EvictionPolicy::kAllKeysRandom),
            "allkeys-random");
  EXPECT_EQ(EvictionPolicyToString(EvictionPolicy::kVolatileRandom),
            "volatile-random");
  EXPECT_EQ(EvictionPolicyToString(EvictionPolicy::kVolatileTTL),
            "volatile-ttl");
}

// Test IsVolatilePolicy
TEST(EvictionPolicyTest, IsVolatilePolicy) {
  EXPECT_FALSE(IsVolatilePolicy(EvictionPolicy::kNoEviction));
  EXPECT_FALSE(IsVolatilePolicy(EvictionPolicy::kAllKeysLRU));
  EXPECT_TRUE(IsVolatilePolicy(EvictionPolicy::kVolatileLRU));
  EXPECT_FALSE(IsVolatilePolicy(EvictionPolicy::kAllKeysLFU));
  EXPECT_TRUE(IsVolatilePolicy(EvictionPolicy::kVolatileLFU));
  EXPECT_FALSE(IsVolatilePolicy(EvictionPolicy::kAllKeysRandom));
  EXPECT_TRUE(IsVolatilePolicy(EvictionPolicy::kVolatileRandom));
  EXPECT_TRUE(IsVolatilePolicy(EvictionPolicy::kVolatileTTL));
}

// Test IsEvictionActive
TEST(EvictionPolicyTest, IsEvictionActive) {
  EXPECT_FALSE(IsEvictionActive(EvictionPolicy::kNoEviction));
  EXPECT_TRUE(IsEvictionActive(EvictionPolicy::kAllKeysLRU));
  EXPECT_TRUE(IsEvictionActive(EvictionPolicy::kVolatileLRU));
  EXPECT_TRUE(IsEvictionActive(EvictionPolicy::kAllKeysLFU));
  EXPECT_TRUE(IsEvictionActive(EvictionPolicy::kVolatileLFU));
  EXPECT_TRUE(IsEvictionActive(EvictionPolicy::kAllKeysRandom));
  EXPECT_TRUE(IsEvictionActive(EvictionPolicy::kVolatileRandom));
  EXPECT_TRUE(IsEvictionActive(EvictionPolicy::kVolatileTTL));
}

// Test All supported eviction policies
TEST(EvictionPolicyTest, AllEvictionPolicies) {
  const std::vector<EvictionPolicy> all_policies = {
      EvictionPolicy::kNoEviction,     EvictionPolicy::kAllKeysLRU,
      EvictionPolicy::kVolatileLRU,    EvictionPolicy::kAllKeysLFU,
      EvictionPolicy::kVolatileLFU,    EvictionPolicy::kAllKeysRandom,
      EvictionPolicy::kVolatileRandom, EvictionPolicy::kVolatileTTL};

  for (auto policy : all_policies) {
    std::string policy_str = EvictionPolicyToString(policy);
    EvictionPolicy policy_from_str = StringToEvictionPolicy(policy_str);
    EXPECT_EQ(policy, policy_from_str)
        << "Policy roundtrip failed for: " << policy_str;
  }
}

}  // namespace astra::core::memory
