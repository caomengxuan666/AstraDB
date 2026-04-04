// 2Q Eviction Strategy - Based on Dragonfly's cache design
// ==============================================================================
// Inspired by Dragonfly Dash-Cache and the 2Q algorithm from 1994 paper
// "2Q: A Low Overhead High Performance Buffer Management Replacement Algorithm"
// ==============================================================================
#pragma once

#include <algorithm>
#include <deque>
#include <random>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "astra/storage/key_metadata.hpp"
#include "eviction_policy.hpp"

namespace astra::core::memory {

// 2Q Eviction Strategy
// - New items enter probationary buffer (FIFO)
// - Items accessed at least once are promoted to protected buffer (LRU)
// - Only 6.7% of space is allocated to probationary buffer (like Dragonfly)
class EvictionStrategy2Q {
 public:
  EvictionStrategy2Q(double probationary_ratio = 0.067)
      : probationary_ratio_(probationary_ratio) {
    if (probationary_ratio <= 0.0 || probationary_ratio >= 1.0) {
      probationary_ratio_ = 0.067;  // Default to 6.7%
    }
  }

  ~EvictionStrategy2Q() = default;

  // Record access to a key (promote if in probationary buffer)
  void RecordAccess(const std::string& key, bool key_exists) {
    absl::MutexLock lock(&mutex_);

    if (!key_exists) {
      // New key - add to probationary buffer
      probationary_queue_.push_back(key);
      key_state_[key] = KeyState::kProbationary;
    } else {
      // Existing key - check if it's in probationary buffer
      auto it = key_state_.find(key);
      if (it != key_state_.end() && it->second == KeyState::kProbationary) {
        // Promote to protected buffer
        PromoteToProtected(key);
      } else if (it != key_state_.end() && it->second == KeyState::kProtected) {
        // Update access time in protected buffer (move to end)
        auto prot_it =
            std::find(protected_queue_.begin(), protected_queue_.end(), key);
        if (prot_it != protected_queue_.end()) {
          protected_queue_.erase(prot_it);
          protected_queue_.push_back(key);
        }
      }
    }
  }

  // Get victim key for eviction
  std::string GetVictim(size_t total_keys) {
    absl::MutexLock lock(&mutex_);

    // Calculate probationary buffer size
    size_t probationary_size =
        static_cast<size_t>(total_keys * probationary_ratio_);
    size_t probationary_count =
        std::min(probationary_size, probationary_queue_.size());

    // First, try to evict from probationary buffer (FIFO)
    if (probationary_count > 0) {
      std::string victim = probationary_queue_.front();
      probationary_queue_.pop_front();
      key_state_.erase(victim);
      return victim;
    }

    // If probationary buffer is empty, evict from protected buffer (LRU)
    if (!protected_queue_.empty()) {
      std::string victim = protected_queue_.front();
      protected_queue_.pop_front();
      key_state_.erase(victim);
      return victim;
    }

    return "";
  }

  // Remove a key from the strategy
  void RemoveKey(const std::string& key) {
    absl::MutexLock lock(&mutex_);

    auto it = key_state_.find(key);
    if (it != key_state_.end()) {
      if (it->second == KeyState::kProbationary) {
        // Remove from probationary queue
        probationary_queue_.erase(std::remove(probationary_queue_.begin(),
                                              probationary_queue_.end(), key),
                                  probationary_queue_.end());
      } else {
        // Remove from protected queue
        protected_queue_.erase(
            std::remove(protected_queue_.begin(), protected_queue_.end(), key),
            protected_queue_.end());
      }
      key_state_.erase(it);
    }
  }

  // Get current probationary buffer size
  size_t GetProbationarySize() const {
    absl::MutexLock lock(&mutex_);
    return probationary_queue_.size();
  }

  // Get current protected buffer size
  size_t GetProtectedSize() const {
    absl::MutexLock lock(&mutex_);
    return protected_queue_.size();
  }

 private:
  // Promote a key from probationary to protected buffer
  void PromoteToProtected(const std::string& key) {
    // Remove from probationary queue
    probationary_queue_.erase(std::remove(probationary_queue_.begin(),
                                          probationary_queue_.end(), key),
                              probationary_queue_.end());
    key_state_[key] = KeyState::kProtected;

    // Evict one key from protected buffer if it's too full
    // This maintains the 6.7% ratio for probationary buffer
    size_t total_keys = key_state_.size();
    size_t probationary_size =
        static_cast<size_t>(total_keys * probationary_ratio_);
    size_t protected_max = total_keys - probationary_size;

    if (protected_queue_.size() >= protected_max && !protected_queue_.empty()) {
      // Demote least recently used key back to probationary
      std::string demoted = protected_queue_.front();
      protected_queue_.pop_front();
      probationary_queue_.push_back(demoted);
      key_state_[demoted] = KeyState::kProbationary;
    }

    // Add promoted key to protected buffer (end = most recently used)
    protected_queue_.push_back(key);
  }

  enum class KeyState {
    kProbationary,  // In probationary buffer (new keys)
    kProtected      // In protected buffer (accessed at least once)
  };

  double probationary_ratio_;
  std::deque<std::string> probationary_queue_;  // FIFO queue for new keys
  std::deque<std::string> protected_queue_;     // LRU queue for accessed keys
  absl::flat_hash_map<std::string, KeyState> key_state_;
  mutable absl::Mutex mutex_;
};

// Memory Sampling Estimator
// - Uses sampling to estimate memory usage instead of exact calculation
// - Reduces overhead compared to exact size calculation
class MemorySamplingEstimator {
 public:
  MemorySamplingEstimator(size_t sample_size = 100)
      : sample_size_(sample_size),
        rng_(std::random_device{}()),
        dist_(0.0, 1.0) {}

  ~MemorySamplingEstimator() = default;

  // Estimate total memory usage from sampled keys
  uint64_t EstimateTotalMemory(
      const std::vector<std::string>& all_keys,
      const std::function<uint32_t(const std::string&)>& get_key_size) {
    if (all_keys.empty()) {
      return 0;
    }

    // If number of keys is small, calculate exactly
    if (all_keys.size() <= sample_size_) {
      uint64_t total = 0;
      for (const auto& key : all_keys) {
        total += get_key_size(key);
      }
      return total;
    }

    // Sample keys and estimate total
    uint64_t sample_total = 0;
    std::vector<size_t> sampled_indices;

    // Randomly sample keys
    for (size_t i = 0; i < sample_size_; ++i) {
      size_t idx = static_cast<size_t>(dist_(rng_) * all_keys.size());
      sampled_indices.push_back(idx);
    }

    // Calculate sample total
    for (size_t idx : sampled_indices) {
      sample_total += get_key_size(all_keys[idx]);
    }

    // Estimate total by extrapolating
    double avg_size = static_cast<double>(sample_total) / sample_size_;
    return static_cast<uint64_t>(avg_size * all_keys.size());
  }

  // Set sample size
  void SetSampleSize(size_t sample_size) {
    if (sample_size > 0) {
      sample_size_ = sample_size;
    }
  }

  // Get current sample size
  size_t GetSampleSize() const { return sample_size_; }

 private:
  size_t sample_size_;
  std::mt19937 rng_;
  std::uniform_real_distribution<double> dist_;
};

}  // namespace astra::core::memory
