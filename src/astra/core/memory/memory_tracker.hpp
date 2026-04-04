// ==============================================================================
// Memory Tracker - Track memory usage for NO SHARING architecture
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "astra/storage/key_metadata.hpp"
#include "eviction_policy.hpp"
#include "object_size_estimator.hpp"

namespace astra::core::memory {

// Memory tracker configuration
struct MemoryTrackerConfig {
  uint64_t max_memory_limit = 0;  // 0 = no limit
  EvictionPolicy eviction_policy = EvictionPolicy::kNoEviction;
  double eviction_threshold = 0.9;  // Trigger eviction at 90% of max memory
  uint32_t eviction_samples = 5;    // Number of samples for LRU/LFU
  bool enable_tracking = true;      // Enable/disable memory tracking
};

// Memory tracker - tracks memory usage per shard
class MemoryTracker {
 public:
  explicit MemoryTracker(
      const MemoryTrackerConfig& config = MemoryTrackerConfig());
  ~MemoryTracker() = default;

  // Disable copy and move
  MemoryTracker(const MemoryTracker&) = delete;
  MemoryTracker& operator=(const MemoryTracker&) = delete;
  MemoryTracker(MemoryTracker&&) = delete;
  MemoryTracker& operator=(MemoryTracker&&) = delete;

  // ========== Memory Tracking ==========

  // Add memory usage (called when inserting/updating data)
  void AddMemory(uint64_t bytes) {
    if (!config_.enable_tracking) return;
    current_memory_.fetch_add(bytes, std::memory_order_relaxed);
  }

  // Subtract memory usage (called when deleting data)
  void SubtractMemory(uint64_t bytes) {
    if (!config_.enable_tracking) return;
    uint64_t old_value = current_memory_.load(std::memory_order_relaxed);
    uint64_t new_value = (bytes > old_value) ? 0 : (old_value - bytes);
    current_memory_.store(new_value, std::memory_order_relaxed);
  }

  // Update memory usage (for replace operations)
  void UpdateMemory(uint64_t old_bytes, uint64_t new_bytes) {
    if (!config_.enable_tracking) return;
    if (new_bytes > old_bytes) {
      AddMemory(new_bytes - old_bytes);
    } else if (old_bytes > new_bytes) {
      SubtractMemory(old_bytes - new_bytes);
    }
  }

  // Get current memory usage
  uint64_t GetCurrentMemory() const {
    return current_memory_.load(std::memory_order_relaxed);
  }

  // Get max memory limit
  uint64_t GetMaxMemory() const { return config_.max_memory_limit; }

  // Check if memory tracking is enabled
  bool IsTrackingEnabled() const { return config_.enable_tracking; }

  // ========== Eviction Control ==========

  // Check if eviction is needed (optimized: only check when memory is close to
  // threshold)
  bool ShouldEvict() const {
    if (!config_.enable_tracking) return false;
    if (config_.max_memory_limit == 0) return false;  // No limit
    if (!IsEvictionActive(config_.eviction_policy)) return false;

    uint64_t current = GetCurrentMemory();
    return current >= config_.max_memory_limit;
  }

  // Check if we should even perform an eviction check (performance
  // optimization) Only check when memory is close to threshold (e.g., within
  // 80% of max)
  bool ShouldCheckEviction() const {
    if (!config_.enable_tracking) return false;
    if (config_.max_memory_limit == 0) return false;  // No limit
    if (!IsEvictionActive(config_.eviction_policy)) return false;

    uint64_t current = GetCurrentMemory();
    // Only check when memory is at 80% of max or higher
    uint64_t check_threshold =
        static_cast<uint64_t>(config_.max_memory_limit * 0.8);
    return current >= check_threshold;
  }

  // Check if memory is at or above limit
  bool IsMemoryFull() const {
    if (!config_.enable_tracking) return false;
    if (config_.max_memory_limit == 0) return false;  // No limit

    uint64_t current = GetCurrentMemory();
    return current >= config_.max_memory_limit;
  }

  // Get eviction policy
  EvictionPolicy GetEvictionPolicy() const { return config_.eviction_policy; }

  // Get eviction threshold
  double GetEvictionThreshold() const { return config_.eviction_threshold; }

  // Get number of samples for LRU/LFU
  uint32_t GetEvictionSamples() const { return config_.eviction_samples; }

  // Check if only volatile keys should be evicted
  bool ShouldEvictVolatileOnly() const {
    return IsVolatilePolicy(config_.eviction_policy);
  }

  // ========== Configuration ==========

  // Update max memory limit
  void SetMaxMemory(uint64_t max_memory) {
    config_.max_memory_limit = max_memory;
  }

  // Update eviction policy
  void SetEvictionPolicy(EvictionPolicy policy) {
    config_.eviction_policy = policy;
  }

  // Update eviction threshold
  void SetEvictionThreshold(double threshold) {
    if (threshold > 0.0 && threshold <= 1.0) {
      config_.eviction_threshold = threshold;
    }
  }

  // Update eviction samples
  void SetEvictionSamples(uint32_t samples) {
    if (samples > 0 && samples <= 100) {
      config_.eviction_samples = samples;
    }
  }

  // Enable/disable memory tracking
  void SetTrackingEnabled(bool enabled) { config_.enable_tracking = enabled; }

  // Get current configuration
  const MemoryTrackerConfig& GetConfig() const { return config_; }

  // ========== Statistics ==========

  // Get memory usage percentage (0.0 to 1.0)
  double GetMemoryUsagePercentage() const {
    if (config_.max_memory_limit == 0) return 0.0;
    uint64_t current = GetCurrentMemory();
    return static_cast<double>(current) /
           static_cast<double>(config_.max_memory_limit);
  }

  // Get free memory (0 if no limit)
  uint64_t GetFreeMemory() const {
    if (config_.max_memory_limit == 0) return 0;
    uint64_t current = GetCurrentMemory();
    return (current < config_.max_memory_limit)
               ? (config_.max_memory_limit - current)
               : 0;
  }

  // Get memory usage in human-readable format
  std::string GetMemoryUsageHuman() const {
    return FormatBytes(GetCurrentMemory());
  }

  // Get max memory in human-readable format
  std::string GetMaxMemoryHuman() const {
    return FormatBytes(config_.max_memory_limit);
  }

  // Reset memory tracker (for testing/debugging)
  void Reset() { current_memory_.store(0, std::memory_order_relaxed); }

 private:
  // Format bytes to human-readable string
  static std::string FormatBytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit_index < 4) {
      size /= 1024.0;
      unit_index++;
    }

    char buffer[64];
    if (unit_index == 0) {
      snprintf(buffer, sizeof(buffer), "%llu %s",
               static_cast<unsigned long long>(bytes), units[unit_index]);
    } else {
      snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unit_index]);
    }

    return std::string(buffer);
  }

  MemoryTrackerConfig config_;
  std::atomic<uint64_t> current_memory_{0};
};

// MemoryTracker constructor implementation
inline MemoryTracker::MemoryTracker(const MemoryTrackerConfig& config)
    : config_(config), current_memory_(0) {}

// Memory Tracker Helper - Simplifies memory management for commands
class MemoryTrackerHelper {
 public:
  // Update memory for String type
  static void UpdateString(MemoryTracker* tracker,
                           astra::storage::KeyMetadataManager* metadata,
                           const std::string& key, const std::string& old_value,
                           const std::string& new_value);

  // Update memory for Hash field
  static void UpdateHashField(MemoryTracker* tracker,
                              astra::storage::KeyMetadataManager* metadata,
                              const std::string& key, bool field_existed,
                              const std::string& old_field_value,
                              const std::string& new_field_value);

  // Update memory for Set member
  static void UpdateSetMember(MemoryTracker* tracker,
                              astra::storage::KeyMetadataManager* metadata,
                              const std::string& key, bool member_existed,
                              const std::string& member);

  // Update memory for ZSet member
  static void UpdateZSetMember(MemoryTracker* tracker,
                               astra::storage::KeyMetadataManager* metadata,
                               const std::string& key, bool member_existed,
                               const std::string& member, double score);

  // Update memory for List element
  static void UpdateListElement(MemoryTracker* tracker,
                                astra::storage::KeyMetadataManager* metadata,
                                const std::string& key, bool element_existed,
                                const std::string& element);
};

// Implementation of MemoryTrackerHelper methods
inline void MemoryTrackerHelper::UpdateString(
    MemoryTracker* tracker, astra::storage::KeyMetadataManager* metadata,
    const std::string& key, const std::string& old_value,
    const std::string& new_value) {
  if (!tracker || !metadata) return;

  uint32_t old_size = ObjectSizeEstimator::EstimateStringSize(key, old_value);
  uint32_t new_size = ObjectSizeEstimator::EstimateStringSize(key, new_value);

  if (old_size != new_size) {
    tracker->UpdateMemory(old_size, new_size);
  }

  metadata->UpdateEstimatedSize(key, new_size);
}

inline void MemoryTrackerHelper::UpdateHashField(
    MemoryTracker* tracker, astra::storage::KeyMetadataManager* metadata,
    const std::string& key, bool field_existed,
    const std::string& old_field_value, const std::string& new_field_value) {
  if (!tracker || !metadata) return;

  // Get old total hash size
  uint32_t old_size = metadata->GetEstimatedSize(key);

  // Calculate new total hash size
  uint32_t field_size_delta = 0;
  if (field_existed) {
    field_size_delta = static_cast<uint32_t>(new_field_value.size()) -
                       static_cast<uint32_t>(old_field_value.size());
  } else {
    field_size_delta = static_cast<uint32_t>(new_field_value.size()) +
                       static_cast<uint32_t>(key.size()) + 8;  // field overhead
  }

  uint32_t new_size = old_size + field_size_delta;

  if (old_size != new_size) {
    tracker->UpdateMemory(old_size, new_size);
  }

  metadata->UpdateEstimatedSize(key, new_size);
}

inline void MemoryTrackerHelper::UpdateSetMember(
    MemoryTracker* tracker, astra::storage::KeyMetadataManager* metadata,
    const std::string& key, bool member_existed, const std::string& member) {
  if (!tracker || !metadata) return;

  uint32_t old_size = metadata->GetEstimatedSize(key);
  uint32_t member_size =
      static_cast<uint32_t>(member.size()) + 8;  // member overhead

  uint32_t new_size = member_existed ? old_size : (old_size + member_size);

  if (old_size != new_size) {
    tracker->UpdateMemory(old_size, new_size);
  }

  metadata->UpdateEstimatedSize(key, new_size);
}

inline void MemoryTrackerHelper::UpdateZSetMember(
    MemoryTracker* tracker, astra::storage::KeyMetadataManager* metadata,
    const std::string& key, bool member_existed, const std::string& member,
    double score) {
  if (!tracker || !metadata) return;

  uint32_t old_size = metadata->GetEstimatedSize(key);
  uint32_t member_size =
      static_cast<uint32_t>(member.size()) + 16;  // member + score overhead

  uint32_t new_size = member_existed ? old_size : (old_size + member_size);

  if (old_size != new_size) {
    tracker->UpdateMemory(old_size, new_size);
  }

  metadata->UpdateEstimatedSize(key, new_size);
}

inline void MemoryTrackerHelper::UpdateListElement(
    MemoryTracker* tracker, astra::storage::KeyMetadataManager* metadata,
    const std::string& key, bool element_existed, const std::string& element) {
  if (!tracker || !metadata) return;

  uint32_t old_size = metadata->GetEstimatedSize(key);
  uint32_t element_size =
      static_cast<uint32_t>(element.size()) + 8;  // element overhead

  uint32_t new_size = element_existed ? old_size : (old_size + element_size);

  if (old_size != new_size) {
    tracker->UpdateMemory(old_size, new_size);
  }

  metadata->UpdateEstimatedSize(key, new_size);
}

}  // namespace astra::core::memory
