// ==============================================================================
// Eviction Policy Definitions
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <string>

namespace astra::core::memory {

// Eviction policy types (Redis-compatible + Dragonfly-inspired)
enum class EvictionPolicy : uint8_t {
  kNoEviction = 0,         // No eviction, return error on OOM
  kAllKeysLRU = 1,         // Evict any key using LRU
  kVolatileLRU = 2,        // Evict keys with TTL using LRU
  kAllKeysLFU = 3,         // Evict any key using LFU
  kVolatileLFU = 4,        // Evict keys with TTL using LFU
  kAllKeysRandom = 5,      // Evict any key randomly
  kVolatileRandom = 6,     // Evict keys with TTL randomly
  kVolatileTTL = 7,        // Evict keys with smallest TTL
  k2Q = 8                  // Dragonfly-inspired 2Q algorithm (recommended)
};

// Convert eviction policy to string
inline std::string EvictionPolicyToString(EvictionPolicy policy) {
  switch (policy) {
    case EvictionPolicy::kNoEviction:
      return "noeviction";
    case EvictionPolicy::kAllKeysLRU:
      return "allkeys-lru";
    case EvictionPolicy::kVolatileLRU:
      return "volatile-lru";
    case EvictionPolicy::kAllKeysLFU:
      return "allkeys-lfu";
    case EvictionPolicy::kVolatileLFU:
      return "volatile-lfu";
    case EvictionPolicy::kAllKeysRandom:
      return "allkeys-random";
    case EvictionPolicy::kVolatileRandom:
      return "volatile-random";
    case EvictionPolicy::kVolatileTTL:
      return "volatile-ttl";
    case EvictionPolicy::k2Q:
      return "2q";
    default:
      return "unknown";
  }
}

// Convert string to eviction policy
inline EvictionPolicy StringToEvictionPolicy(const std::string& str) {
  std::string lower_str = str;
  for (auto& c : lower_str) {
    c = std::tolower(c);
  }

  if (lower_str == "noeviction") {
    return EvictionPolicy::kNoEviction;
  } else if (lower_str == "allkeys-lru") {
    return EvictionPolicy::kAllKeysLRU;
  } else if (lower_str == "volatile-lru") {
    return EvictionPolicy::kVolatileLRU;
  } else if (lower_str == "allkeys-lfu") {
    return EvictionPolicy::kAllKeysLFU;
  } else if (lower_str == "volatile-lfu") {
    return EvictionPolicy::kVolatileLFU;
  } else if (lower_str == "allkeys-random") {
    return EvictionPolicy::kAllKeysRandom;
  } else if (lower_str == "volatile-random") {
    return EvictionPolicy::kVolatileRandom;
  } else if (lower_str == "volatile-ttl") {
    return EvictionPolicy::kVolatileTTL;
  } else if (lower_str == "2q" || lower_str == "dash-cache") {
    return EvictionPolicy::k2Q;
  } else {
    return EvictionPolicy::kNoEviction;  // Default to noeviction
  }
}

// Check if policy only evicts keys with TTL
inline bool IsVolatilePolicy(EvictionPolicy policy) {
  return policy == EvictionPolicy::kVolatileLRU ||
         policy == EvictionPolicy::kVolatileLFU ||
         policy == EvictionPolicy::kVolatileRandom ||
         policy == EvictionPolicy::kVolatileTTL;
}

// Check if policy is active (requires eviction)
inline bool IsEvictionActive(EvictionPolicy policy) {
  return policy != EvictionPolicy::kNoEviction;
}

// Get default eviction policy (noeviction for backward compatibility)
inline constexpr EvictionPolicy GetDefaultEvictionPolicy() {
  return EvictionPolicy::kNoEviction;
}

}  // namespace astra::core::memory