// ==============================================================================
// Shard Manager - Data Sharding and Distribution
// ==============================================================================
// License: Apache 2.0
// ==============================================================================
// Design Principles:
// - noexcept for all performance-critical paths
// - Abseil containers instead of STL
// - Consistent hashing for shard distribution
// - Cross-platform: Linux/Windows/macOS
// ==============================================================================

#pragma once

#include "gossip_manager.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/btree_map.h>
#include <absl/strings/string_view.h>
#include <absl/hash/hash.h>
#include <absl/synchronization/mutex.h>

#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <cmath>

#include "astra/base/macros.hpp"
#include "astra/base/logging.hpp"

namespace astra::cluster {

// Hash slot count (16384 slots, same as Redis Cluster)
constexpr uint32_t kHashSlotCount = 16384;

// Hash slot type
using HashSlot = uint16_t;

// Shard ID type
using ShardId = uint32_t;

// ==============================================================================
// Hash Slot Calculator
// ==============================================================================

class HashSlotCalculator {
 public:
  // Calculate hash slot for a key (CRC16 based, Redis compatible)
  static HashSlot Calculate(absl::string_view key) noexcept {
    return static_cast<HashSlot>(CRC16(key.data(), key.size()) % kHashSlotCount);
  }

  // Calculate hash slot with hash tag support
  // For example: key{tag} -> only hash "tag" part
  static HashSlot CalculateWithTag(absl::string_view key) noexcept {
    // Find hash tag {tag}
    size_t start = key.find('{');
    if (start != absl::string_view::npos) {
      size_t end = key.find('}', start + 1);
      if (end != absl::string_view::npos && end > start + 1) {
        // Hash only the tag part
        absl::string_view tag(key.data() + start + 1, end - start - 1);
        return Calculate(tag);
      }
    }
    return Calculate(key);
  }

 private:
  // CRC16 implementation (XMODEM polynomial)
  static uint16_t CRC16(const char* data, size_t len) noexcept {
    static constexpr uint16_t crc16_table[256] = {
      0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
      0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
      0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
      0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
      0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
      0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
      0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
      0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
      0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
      0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
      0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
      0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
      0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
      0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
      0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
      0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
      0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
      0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
      0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
      0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
      0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
      0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
      0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
      0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
      0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
      0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
      0x5b7d, 0x4b5c, 0x7b3f, 0x6b1e, 0x1bf9, 0x0bd8, 0x3bbb, 0x2b9a,
      0xad07, 0xbd26, 0x8d45, 0x9d64, 0xed83, 0xfda2, 0xcde1, 0xddc0,
      0xad6f, 0xbd4e, 0x8d29, 0x9d08, 0xedef, 0xfdce, 0xcda9, 0xdd88,
      0x6966, 0x7947, 0x4924, 0x5905, 0x29e2, 0x39c3, 0x09a0, 0x1981,
      0x9b5e, 0x8b7f, 0xbb1c, 0xab3d, 0xdbda, 0xcbfb, 0xfb98, 0xebb9,
      0x6a56, 0x7a77, 0x4a14, 0x5a35, 0x2ad2, 0x3af3, 0x0a90, 0x1ab1,
    };
    
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
      crc = static_cast<uint16_t>((crc << 8) ^ crc16_table[(crc >> 8) ^ static_cast<uint8_t>(data[i])]);
    }
    return crc;
  }
};

// ==============================================================================
// Shard Configuration
// ==============================================================================

struct ShardConfig {
  ShardId id = 0;
  HashSlot slot_start = 0;
  HashSlot slot_end = 0;
  NodeId primary_node{};         // Primary node ID
  std::vector<NodeId> replicas; // Replica node IDs
  
  // Status
  enum class State : uint8_t {
    kStable,
    kMigrating,
    kImporting,
  };
  State state = State::kStable;
  
  // Migration target (if migrating)
  NodeId migration_target{};
};

// ==============================================================================
// Shard Manager - Manages shard distribution and routing
// ==============================================================================

class ShardManager {
 public:
  ShardManager() noexcept = default;
  ~ShardManager() noexcept = default;

  // Non-copyable
  ShardManager(const ShardManager&) = delete;
  ShardManager& operator=(const ShardManager&) = delete;

  // Movable
  ShardManager(ShardManager&&) noexcept = default;
  ShardManager& operator=(ShardManager&&) noexcept = default;

  // Initialize with number of shards
  bool Init(uint32_t shard_count, const NodeId& self_id) noexcept {
    if (shard_count == 0 || shard_count > kHashSlotCount) {
      ASTRADB_LOG_ERROR("Invalid shard count: {}", shard_count);
      return false;
    }
    
    self_id_ = self_id;
    shard_count_ = shard_count;
    
    // Initialize shard configurations
    shards_.clear();
    shards_.reserve(shard_count);
    
    // Distribute hash slots evenly among shards
    const uint32_t slots_per_shard = kHashSlotCount / shard_count;
    uint32_t remaining_slots = kHashSlotCount % shard_count;
    
    HashSlot current_slot = 0;
    for (ShardId i = 0; i < shard_count; ++i) {
      ShardConfig config;
      config.id = i;
      config.slot_start = current_slot;
      
      // Assign slots
      uint32_t slots = slots_per_shard;
      if (remaining_slots > 0) {
        ++slots;
        --remaining_slots;
      }
      
      config.slot_end = static_cast<HashSlot>(current_slot + slots - 1);
      config.primary_node = self_id_;
      
      shards_.push_back(config);
      
      // Build slot -> shard mapping
      for (HashSlot slot = config.slot_start; slot <= config.slot_end; ++slot) {
        slot_to_shard_[slot] = i;
      }
      
      current_slot = static_cast<HashSlot>(config.slot_end + 1);
    }
    
    initialized_.store(true, std::memory_order_release);
    ASTRADB_LOG_INFO("ShardManager initialized: {} shards, {} slots each (approximately)", 
                 shard_count, slots_per_shard);
    return true;
  }

  // ========== Key Routing ==========

  // Get shard ID for a key
  ShardId GetShardForKey(absl::string_view key) const noexcept {
    HashSlot slot = HashSlotCalculator::CalculateWithTag(key);
    return GetShardForSlot(slot);
  }

  // Get shard ID for a hash slot
  ShardId GetShardForSlot(HashSlot slot) const noexcept {
    auto it = slot_to_shard_.find(slot);
    if (ASTRADB_LIKELY(it != slot_to_shard_.end())) {
      return it->second;
    }
    return 0;  // Default to shard 0
  }

  // Get node responsible for a key
  NodeId GetNodeForKey(absl::string_view key) const noexcept {
    ShardId shard = GetShardForKey(key);
    return GetPrimaryNode(shard);
  }

  // Get primary node for a shard
  NodeId GetPrimaryNode(ShardId shard) const noexcept {
    if (ASTRADB_UNLIKELY(shard >= shards_.size())) {
      return self_id_;
    }
    return shards_[shard].primary_node;
  }

  // ========== Shard Management ==========

  // Get shard configuration
  const ShardConfig* GetShard(ShardId id) const noexcept {
    if (id >= shards_.size()) {
      return nullptr;
    }
    return &shards_[id];
  }

  // Get all shards
  const std::vector<ShardConfig>& GetAllShards() const noexcept {
    return shards_;
  }

  // Get shard count
  uint32_t GetShardCount() const noexcept {
    return shard_count_;
  }

  // Check if this node owns a shard
  bool OwnsShard(ShardId shard) const noexcept {
    if (shard >= shards_.size()) {
      return false;
    }
    return shards_[shard].primary_node == self_id_;
  }

  // Check if this node owns a slot
  bool OwnsSlot(HashSlot slot) const noexcept {
    ShardId shard = GetShardForSlot(slot);
    return OwnsShard(shard);
  }

  // ========== Shard Migration ==========

  // Start migrating a shard to another node
  bool StartMigration(ShardId shard, const NodeId& target) noexcept {
    if (shard >= shards_.size()) {
      return false;
    }
    
    shards_[shard].state = ShardConfig::State::kMigrating;
    shards_[shard].migration_target = target;
    
    ASTRADB_LOG_INFO("Started migration of shard {} to {}", shard, 
                 GossipManager::NodeIdToString(target));
    return true;
  }

  // Complete migration
  bool CompleteMigration(ShardId shard) noexcept {
    if (shard >= shards_.size()) {
      return false;
    }
    
    auto& config = shards_[shard];
    config.primary_node = config.migration_target;
    config.state = ShardConfig::State::kStable;
    config.migration_target = NodeId{};
    
    ASTRADB_LOG_INFO("Completed migration of shard {}", shard);
    return true;
  }

  // Cancel migration
  bool CancelMigration(ShardId shard) noexcept {
    if (shard >= shards_.size()) {
      return false;
    }
    
    shards_[shard].state = ShardConfig::State::kStable;
    shards_[shard].migration_target = NodeId{};
    
    ASTRADB_LOG_INFO("Cancelled migration of shard {}", shard);
    return true;
  }

  // ========== Replica Management ==========

  // Add replica to a shard
  bool AddReplica(ShardId shard, const NodeId& replica) noexcept {
    if (shard >= shards_.size()) {
      return false;
    }
    
    auto& replicas = shards_[shard].replicas;
    if (std::find(replicas.begin(), replicas.end(), replica) == replicas.end()) {
      replicas.push_back(replica);
      ASTRADB_LOG_INFO("Added replica to shard {}: {}", shard, 
                   GossipManager::NodeIdToString(replica));
    }
    return true;
  }

  // Remove replica from a shard
  bool RemoveReplica(ShardId shard, const NodeId& replica) noexcept {
    if (shard >= shards_.size()) {
      return false;
    }
    
    auto& replicas = shards_[shard].replicas;
    auto it = std::find(replicas.begin(), replicas.end(), replica);
    if (it != replicas.end()) {
      replicas.erase(it);
      ASTRADB_LOG_INFO("Removed replica from shard {}: {}", shard, 
                   GossipManager::NodeIdToString(replica));
    }
    return true;
  }

  // ========== Statistics ==========

  // Get slot range for a shard
  std::pair<HashSlot, HashSlot> GetSlotRange(ShardId shard) const noexcept {
    if (shard >= shards_.size()) {
      return {0, 0};
    }
    return {shards_[shard].slot_start, shards_[shard].slot_end};
  }

  // Get total slot count for a shard
  uint32_t GetSlotCount(ShardId shard) const noexcept {
    if (shard >= shards_.size()) {
      return 0;
    }
    return shards_[shard].slot_end - shards_[shard].slot_start + 1;
  }

 private:
  NodeId self_id_{};
  uint32_t shard_count_ = 0;
  std::vector<ShardConfig> shards_;
  absl::flat_hash_map<HashSlot, ShardId> slot_to_shard_;
  
  std::atomic<bool> initialized_{false};
};

}  // namespace astra::cluster
