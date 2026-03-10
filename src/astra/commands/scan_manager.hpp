// ==============================================================================
// SCAN Command Fix
// ==============================================================================
// This file contains the fixed implementation of SCAN commands
// ==============================================================================

#pragma once

#include <atomic>
#include <memory>
#include <absl/container/flat_hash_map.h>
#include <absl/synchronization/mutex.h>
#include <string>
#include <vector>

namespace astra::commands {

// Scan state for tracking iteration progress
struct ScanState {
  std::vector<std::string> all_keys;  // All keys from last scan
  size_t current_position;             // Current position in iteration
  uint64_t scan_id;                    // Unique ID for this scan session
  
  ScanState() : current_position(0), scan_id(0) {}
};

// Global scan state manager (simplified for single-server use)
class ScanStateManager {
 public:
  static ScanStateManager& Instance() {
    static ScanStateManager instance;
    return instance;
  }
  
  // Start a new scan session
  uint64_t StartScan(const std::vector<std::string>& keys) {
    absl::MutexLock lock(&mutex_);
    uint64_t scan_id = ++next_scan_id_;
    auto state = std::make_unique<ScanState>();
    state->all_keys = keys;
    state->current_position = 0;
    state->scan_id = scan_id;
    scan_states_[scan_id] = std::move(state);
    return scan_id;
  }
  
  // Get next batch of keys
  std::pair<uint64_t, std::vector<std::string>> GetNextBatch(uint64_t cursor, size_t count) {
    absl::MutexLock lock(&mutex_);
    
    auto it = scan_states_.find(cursor);
    if (it == scan_states_.end()) {
      // Invalid cursor, start fresh
      return {0, {}};
    }
    
    auto& state = it->second;
    if (state->current_position >= state->all_keys.size()) {
      // Scan complete
      scan_states_.erase(it);
      return {0, {}};
    }
    
    // Get next batch
    size_t end_idx = std::min(state->current_position + count, state->all_keys.size());
    std::vector<std::string> batch;
    
    for (size_t i = state->current_position; i < end_idx; ++i) {
      batch.push_back(state->all_keys[i]);
    }
    
    state->current_position = end_idx;
    
    // Return new cursor (same scan_id if more keys, 0 if complete)
    uint64_t new_cursor = (state->current_position < state->all_keys.size()) ? state->scan_id : 0;
    
    if (new_cursor == 0) {
      scan_states_.erase(it);
    }
    
    return {new_cursor, batch};
  }
  
 private:
  ScanStateManager() : next_scan_id_(0) {}
  
  absl::Mutex mutex_;
  std::atomic<uint64_t> next_scan_id_;
  absl::flat_hash_map<uint64_t, std::unique_ptr<ScanState>> scan_states_;
};

}  // namespace astra::commands