// ==============================================================================
// Eviction Manager - Manage key eviction for NO SHARING architecture
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/random/random.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "astra/core/metrics.hpp"
#include "astra/storage/key_metadata.hpp"
#include "eviction_policy.hpp"
#include "eviction_strategy_2q.hpp"
#include "memory_tracker.hpp"

// Forward declarations
namespace astra::storage {
class KeyMetadataManager;
}

namespace astra::commands {
class Database;
}

namespace astra::core::memory {

// Eviction candidate for selection
struct EvictionCandidate {
  std::string key;
  uint32_t access_time_ms;
  uint8_t lfu_counter;
  uint32_t estimated_size;
  std::optional<int64_t> ttl_ms;
  astra::storage::KeyType type;

  EvictionCandidate()
      : access_time_ms(0),
        lfu_counter(0),
        estimated_size(0),
        type(astra::storage::KeyType::kNone) {}
};

// Eviction callback - called when a key is selected for eviction
using EvictionCallback =
    std::function<bool(const std::string& key, astra::storage::KeyType type)>;

// Callback to get total memory usage across all workers
using GetTotalMemoryCallback = std::function<size_t()>;

// Eviction manager - manages key eviction based on memory usage and policy
class EvictionManager {
 public:
  // Constructor with callback for global memory tracking
  EvictionManager(MemoryTracker* memory_tracker,
                  astra::storage::KeyMetadataManager* metadata_manager,
                  GetTotalMemoryCallback get_total_memory_callback = nullptr)
      : memory_tracker_(memory_tracker),
        metadata_manager_(metadata_manager),
        get_total_memory_callback_(std::move(get_total_memory_callback)),
        rng_(absl::BitGen()) {}

  ~EvictionManager() = default;

  // Disable copy and move
  EvictionManager(const EvictionManager&) = delete;
  EvictionManager& operator=(const EvictionManager&) = delete;
  EvictionManager(EvictionManager&&) = delete;
  EvictionManager& operator=(EvictionManager&&) = delete;

  // Set eviction callback (called when a key is selected for eviction)
  void SetEvictionCallback(EvictionCallback callback) {
    eviction_callback_ = std::move(callback);
  }

  // Check if eviction is needed and perform eviction
  // Returns number of keys evicted
  size_t CheckAndEvict() {
    if (!memory_tracker_) {
      return 0;
    }

    EvictionPolicy policy = memory_tracker_->GetEvictionPolicy();

    // Get total memory usage across all workers if callback is available
    size_t current_memory = memory_tracker_->GetCurrentMemory();
    if (get_total_memory_callback_) {
      current_memory = get_total_memory_callback_();
    }

    size_t max_memory = memory_tracker_->GetMaxMemory();
    bool should_evict = ShouldEvict(current_memory, max_memory, policy);

    ASTRADB_LOG_DEBUG(
        "CheckAndEvict: policy={}, current_memory={}, max_memory={}, "
        "should_evict={}",
        static_cast<int>(policy), current_memory, max_memory, should_evict);

    if (!should_evict) {
      return 0;
    }

    if (policy == EvictionPolicy::kNoEviction) {
      ASTRADB_LOG_DEBUG(
          "CheckAndEvict: policy is noeviction, skipping eviction");
      return 0;
    }

    // Evict keys until memory is below threshold
    size_t evicted_count = 0;
    const uint32_t max_iterations = 100;  // Prevent infinite loops
    uint32_t iterations = 0;

    while (ShouldEvict(current_memory, max_memory, policy) &&
           iterations < max_iterations) {
      std::string victim_key = SelectVictim(policy);
      if (victim_key.empty()) {
        break;  // No more keys to evict
      }

      ASTRADB_LOG_DEBUG("CheckAndEvict: selected victim key: {}", victim_key);
      if (ExecuteEviction(victim_key)) {
        evicted_count++;
        ASTRADB_LOG_DEBUG(
            "CheckAndEvict: successfully evicted key: {}, total evicted: {}",
            victim_key, evicted_count);

        // Update current_memory after eviction
        if (get_total_memory_callback_) {
          current_memory = get_total_memory_callback_();
        } else {
          current_memory = memory_tracker_->GetCurrentMemory();
        }
      } else {
        ASTRADB_LOG_DEBUG("CheckAndEvict: failed to evict key: {}", victim_key);
      }

      iterations++;
    }

    ASTRADB_LOG_DEBUG("CheckAndEvict: finished, evicted {} keys",
                      evicted_count);
    return evicted_count;
  }

  // Select victim key based on eviction policy
  std::string SelectVictim(EvictionPolicy policy) {
    // Special handling for 2Q policy
    if (policy == EvictionPolicy::k2Q) {
      Init2QStrategy();
      auto all_keys = metadata_manager_->GetAllKeys();
      return strategy_2q_->GetVictim(all_keys.size());
    }

    // Get all keys from metadata manager
    auto all_keys = metadata_manager_->GetAllKeys();
    if (all_keys.empty()) {
      return "";
    }

    // Filter keys based on policy (volatile vs allkeys)
    std::vector<std::string> candidate_keys;
    bool volatile_only = IsVolatilePolicy(policy);

    for (const auto& key : all_keys) {
      if (volatile_only && !metadata_manager_->HasTTL(key)) {
        continue;  // Skip keys without TTL for volatile policies
      }
      candidate_keys.push_back(key);
    }

    if (candidate_keys.empty()) {
      return "";
    }

    // Select victim based on policy
    switch (policy) {
      case EvictionPolicy::kAllKeysLRU:
      case EvictionPolicy::kVolatileLRU:
        return SelectVictim_LRU(candidate_keys);

      case EvictionPolicy::kAllKeysLFU:
      case EvictionPolicy::kVolatileLFU:
        return SelectVictim_LFU(candidate_keys);

      case EvictionPolicy::kAllKeysRandom:
      case EvictionPolicy::kVolatileRandom:
        return SelectVictim_Random(candidate_keys);

      case EvictionPolicy::kVolatileTTL:
        return SelectVictim_TTL(candidate_keys);

      default:
        return "";
    }
  }

  // Execute eviction for a specific key
  bool ExecuteEviction(const std::string& key) {
    if (!eviction_callback_) {
      return false;
    }

    // Get key type
    auto key_type = metadata_manager_->GetKeyType(key);
    if (!key_type.has_value()) {
      return false;
    }

    // Remove from 2Q strategy if active
    if (strategy_2q_ &&
        memory_tracker_->GetEvictionPolicy() == EvictionPolicy::k2Q) {
      strategy_2q_->RemoveKey(key);
    }

    // Call eviction callback to delete the key
    bool evicted = eviction_callback_(key, *key_type);

    // Record metrics
    if (evicted) {
      astra::metrics::AstraMetrics::Instance().RecordEvictionKey();
    }
    astra::metrics::AstraMetrics::Instance().RecordEvictionOperation();

    return evicted;
  }

  // Get eviction statistics
  struct EvictionStats {
    size_t total_evicted = 0;
    size_t lru_evicted = 0;
    size_t lfu_evicted = 0;
    size_t random_evicted = 0;
    size_t ttl_evicted = 0;
  };

  EvictionStats GetStats() const { return stats_; }

  // Reset statistics
  void ResetStats() { stats_ = EvictionStats(); }

 private:
  // ========== Victim Selection Algorithms ==========

  // LRU: Select key with oldest access time
  std::string SelectVictim_LRU(const std::vector<std::string>& keys) {
    uint32_t num_samples = memory_tracker_->GetEvictionSamples();

    // Sample keys and find the one with oldest access time
    std::string oldest_key;
    uint32_t oldest_access_time = UINT32_MAX;

    for (uint32_t i = 0; i < num_samples; ++i) {
      // Randomly select a key
      size_t idx = absl::Uniform<size_t>(rng_, 0ULL, keys.size());
      const std::string& key = keys[idx];

      uint32_t access_time = metadata_manager_->GetAccessTime(key);
      if (access_time < oldest_access_time) {
        oldest_access_time = access_time;
        oldest_key = key;
      }
    }

    if (!oldest_key.empty()) {
      stats_.lru_evicted++;
    }

    return oldest_key;
  }

  // LFU: Select key with lowest access frequency
  std::string SelectVictim_LFU(const std::vector<std::string>& keys) {
    uint32_t num_samples = memory_tracker_->GetEvictionSamples();

    // Sample keys and find the one with lowest LFU counter
    std::string least_frequent_key;
    uint8_t lowest_lfu = 255;

    for (uint32_t i = 0; i < num_samples; ++i) {
      // Randomly select a key
      size_t idx = absl::Uniform<size_t>(rng_, 0ULL, keys.size());
      const std::string& key = keys[idx];

      uint8_t lfu = metadata_manager_->GetLFUCounter(key);
      if (lfu < lowest_lfu) {
        lowest_lfu = lfu;
        least_frequent_key = key;
      }
    }

    if (!least_frequent_key.empty()) {
      stats_.lfu_evicted++;
    }

    return least_frequent_key;
  }

  // Random: Select a random key
  std::string SelectVictim_Random(const std::vector<std::string>& keys) {
    if (keys.empty()) {
      return "";
    }

    size_t idx = absl::Uniform<size_t>(rng_, 0ULL, keys.size());
    std::string random_key = keys[idx];

    stats_.random_evicted++;
    return random_key;
  }

  // TTL: Select key with smallest TTL (closest to expiration)
  std::string SelectVictim_TTL(const std::vector<std::string>& keys) {
    std::string smallest_ttl_key;
    int64_t smallest_ttl = INT64_MAX;

    // Find key with smallest TTL
    for (const auto& key : keys) {
      int64_t ttl = metadata_manager_->GetTtlMs(key);
      // TTL > 0 means key has expiration
      if (ttl > 0 && ttl < smallest_ttl) {
        smallest_ttl = ttl;
        smallest_ttl_key = key;
      }
    }

    if (!smallest_ttl_key.empty()) {
      stats_.ttl_evicted++;
    }

    return smallest_ttl_key;
  }

  MemoryTracker* memory_tracker_;
  astra::storage::KeyMetadataManager* metadata_manager_;
  GetTotalMemoryCallback get_total_memory_callback_;
  EvictionCallback eviction_callback_;
  EvictionStats stats_;
  absl::BitGen rng_;
  std::unique_ptr<EvictionStrategy2Q> strategy_2q_;  // For 2Q policy

  // Check if eviction is needed
  bool ShouldEvict(size_t current_memory, size_t max_memory,
                   EvictionPolicy policy) {
    if (!memory_tracker_->IsTrackingEnabled()) return false;
    if (max_memory == 0) return false;  // No limit
    if (!IsEvictionActive(policy)) return false;

    double threshold = memory_tracker_->GetEvictionThreshold();
    uint64_t threshold_bytes = static_cast<uint64_t>(max_memory * threshold);
    return current_memory >= threshold_bytes;
  }

  // Initialize 2Q strategy if not already initialized
  void Init2QStrategy() {
    if (!strategy_2q_) {
      strategy_2q_ = std::make_unique<EvictionStrategy2Q>();
    }
  }
};

}  // namespace astra::core::memory
