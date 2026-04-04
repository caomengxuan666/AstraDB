// ==============================================================================
// Gossip Manager - Cluster Membership Management with libgossip
// ==============================================================================
// License: Apache 2.0
// ==============================================================================
// Design Principles:
// - noexcept for all performance-critical paths
// - Abseil containers instead of STL
// - Use libgossip's network layer (transport_factory)
// - Use AstraDB logging macros
// - Cross-platform: Linux/Windows/macOS
// ==============================================================================

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/strings/string_view.h>
#include <absl/synchronization/mutex.h>
#include <absl/time/time.h>

#include <core/logger.hpp>  // For libgossip logging system

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "astra/base/logging.hpp"
#include "astra/base/macros.hpp"
#include "core/gossip_core.hpp"
#include "net/json_serializer.hpp"
#include "net/transport_factory.hpp"

namespace astra::cluster {

// Node ID type (16 bytes, compatible with libgossip)
using NodeId = libgossip::node_id_t;

// Node status
using NodeStatus = libgossip::node_status;

// Node view with additional AstraDB-specific fields
struct AstraNodeView {
  NodeId id;
  std::string ip;
  int port;
  std::string role;       // "master", "replica"
  std::string region;     // Geographic region
  uint64_t config_epoch;  // Configuration version
  uint64_t heartbeat;     // Logical heartbeat
  uint64_t version;       // Data version
  NodeStatus status;

  // AstraDB-specific fields
  uint32_t shard_count;  // Number of shards this node manages
  uint64_t memory_used;  // Memory usage in bytes
  uint64_t keys_count;   // Total key count

  // Metadata
  absl::flat_hash_map<std::string, std::string> metadata;
};

// Cluster configuration
struct ClusterConfig {
  std::string node_id;  // Unique node ID (hex string)
  std::string bind_ip = "0.0.0.0";
  int gossip_port = 7379;  // Gossip protocol port
  int data_port = 6379;    // Data service port

  // Timing configuration
  int heartbeat_interval_ms = 100;  // Heartbeat interval
  int failure_timeout_ms = 2000;    // Failure detection timeout
  int cleanup_timeout_ms = 60000;   // Cleanup expired nodes timeout

  // Gossip configuration
  int gossip_nodes = 3;  // Number of nodes to gossip with per tick
  int sync_nodes = 2;    // Number of nodes to sync per message

  // Transport type
  bool use_tcp = false;  // false = UDP, true = TCP

  // Cluster settings
  std::string region;           // Geographic region
  std::string role = "master";  // "master" or "replica"
  uint32_t shard_count = 1;     // Number of shards on this node
};

// Cluster event types
enum class ClusterEvent : uint8_t {
  kNodeJoined,     // New node joined
  kNodeLeft,       // Node left gracefully
  kNodeFailed,     // Node failed
  kNodeRecovered,  // Node recovered from suspect
  kLeaderChanged,  // Leader changed
  kConfigChanged,  // Cluster config changed
};

// Event callback type - use std::function to own the callback object
using ClusterEventCallback = std::function<void(ClusterEvent, const AstraNodeView&)>;

// ==============================================================================
// GossipManager - Manages cluster membership using SWIM protocol
// ==============================================================================

class GossipManager {
 public:
  GossipManager() noexcept = default;
  ~GossipManager() noexcept { Stop(); }

  // Non-copyable
  GossipManager(const GossipManager&) = delete;
  GossipManager& operator=(const GossipManager&) = delete;

  // Non-movable (std::atomic is not movable)
  GossipManager(GossipManager&&) noexcept = delete;
  GossipManager& operator=(GossipManager&&) noexcept = delete;

  // Initialize with configuration
  bool Init(const ClusterConfig& config) noexcept {
    config_ = config;

    // Parse node ID
    if (!ParseNodeId(config.node_id, self_id_)) {
      // Generate random node ID if not provided
      GenerateNodeId(self_id_);
    }

    // Create self node view
    libgossip::node_view self_view;
    self_view.id = self_id_;
    self_view.ip = config.bind_ip;
    self_view.port = config.gossip_port;
    self_view.role = config.role;
    self_view.region = config.region;
    self_view.config_epoch = 1;
    self_view.heartbeat = 1;
    self_view.version = 1;
    self_view.status = libgossip::node_status::joining;

    // Initialize slot metadata
    self_view.metadata["slots"] = "";
    self_view.metadata["config_epoch"] = "1";

    // Create gossip core with callbacks
    try {
      gossip_core_ = std::make_shared<libgossip::gossip_core>(
          self_view,
          [this](const libgossip::gossip_message& msg,
                 const libgossip::node_view& target) {
            OnSendMessage(msg, target);
          },
          [this](const libgossip::node_view& node,
                 libgossip::node_status old_status) {
            OnNodeEvent(node, old_status);
          });
    } catch (const std::exception& e) {
      ASTRADB_LOG_ERROR("Failed to create gossip core: {}", e.what());
      return false;
    }

    // Create transport using libgossip's transport factory
    auto transport_type = config.use_tcp ? gossip::net::transport_type::tcp
                                         : gossip::net::transport_type::udp;

    transport_ = gossip::net::transport_factory::create_transport(
        transport_type, config.bind_ip,
        static_cast<uint16_t>(config.gossip_port));

    if (!transport_) {
      ASTRADB_LOG_ERROR("Failed to create transport on {}:{}", config.bind_ip,
                        config.gossip_port);
      return false;
    }

    // Create JSON serializer
    auto serializer = std::make_unique<gossip::net::json_serializer>();
    transport_->set_serializer(std::move(serializer));

    // Set gossip core for transport (for receiving messages)
    transport_->set_gossip_core(gossip_core_);

    initialized_.store(true, std::memory_order_release);
    ASTRADB_LOG_INFO("GossipManager initialized: node_id={}, ip={}:{}",
                     NodeIdToString(self_id_), config.bind_ip,
                     config.gossip_port);
    return true;
  }

  // Start the gossip service
  bool Start() noexcept {
    if (ASTRADB_UNLIKELY(!initialized_.load(std::memory_order_acquire))) {
      ASTRADB_LOG_ERROR("GossipManager not initialized");
      return false;
    }

    // Start the transport
    auto result = transport_->start();
    if (result != gossip::net::error_code::success) {
      ASTRADB_LOG_ERROR("Failed to start transport");
      return false;
    }

    running_.store(true, std::memory_order_release);
    ASTRADB_LOG_INFO("GossipManager started");
    return true;
  }

  // Stop the gossip service
  void Stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
      return;  // Already stopped
    }

    // CRITICAL: Clear event callback BEFORE stopping transport
    // This prevents callbacks from executing while Server is being destroyed
    {
      std::lock_guard<std::mutex> lock(event_callback_mutex_);
      event_callback_set_.store(false, std::memory_order_release);
      event_callback_ = nullptr;
    }

    if (transport_) {
      transport_->stop();
    }

    gossip_core_.reset();
    transport_.reset();

    ASTRADB_LOG_INFO("GossipManager stopped");
  }

  // Drive one gossip tick (should be called periodically, e.g., every 100ms)
  void Tick() noexcept {
    if (ASTRADB_UNLIKELY(!running_.load(std::memory_order_acquire))) {
      return;
    }

    if (gossip_core_) {
      gossip_core_->tick();
    }
  }

  // Full broadcast for critical config changes
  void BroadcastConfig() noexcept {
    if (ASTRADB_UNLIKELY(!running_.load(std::memory_order_acquire))) {
      return;
    }

    if (gossip_core_) {
      gossip_core_->tick_full_broadcast();
    }
  }

  // Update slot metadata and broadcast changes
  void UpdateSlotMetadata(const std::string& slots_str) noexcept {
    if (ASTRADB_UNLIKELY(!gossip_core_)) {
      ASTRADB_LOG_WARN("GossipManager not initialized, cannot update slot metadata");
      return;
    }

    // Store locally
    slot_metadata_ = slots_str;
    config_epoch_++;

    ASTRADB_LOG_DEBUG("UpdateSlotMetadata: slots_str='{}', config_epoch={}", slots_str, config_epoch_);

    // Update gossip_core's self metadata directly
    std::map<std::string, std::string> metadata;
    metadata["slots"] = slots_str;
    metadata["config_epoch"] = std::to_string(config_epoch_);
    gossip_core_->update_self_metadata(metadata);

    ASTRADB_LOG_DEBUG("UpdateSlotMetadata: Called gossip_core_->update_self_metadata()");
    
    // Verify update by checking self() metadata
    auto self = gossip_core_->self();
    ASTRADB_LOG_DEBUG("UpdateSlotMetadata: After update, self.metadata['slots'] size={}, content='{}', heartbeat={}, config_epoch={}",
                     self.metadata.contains("slots") ? self.metadata.at("slots").size() : 0,
                     self.metadata.contains("slots") ? self.metadata.at("slots") : "(not found)",
                     self.heartbeat,
                     self.config_epoch);

    // Trigger a gossip tick to immediately propagate the updated metadata
    gossip_core_->tick();
    ASTRADB_LOG_DEBUG("UpdateSlotMetadata: Called gossip_core_->tick() to propagate metadata");
  }

  // Increment config epoch and broadcast
  void IncrementConfigEpoch() noexcept {
    config_epoch_++;
    ASTRADB_LOG_INFO("Config epoch incremented to {}", config_epoch_);
  }

  // Get current config epoch
  uint64_t GetConfigEpoch() const noexcept {
    return config_epoch_;
  }

  // Get slot metadata string
  std::string GetSlotMetadata() const noexcept {
    return slot_metadata_;
  }

  // ========== Node Management ==========

  // Meet a new node (add to cluster)
  bool MeetNode(std::string_view ip, int port) noexcept {
    if (ASTRADB_UNLIKELY(!running_.load(std::memory_order_acquire))) {
      return false;
    }

    libgossip::node_view node;
    node.ip = std::string(ip);
    node.port = port;
    node.status = libgossip::node_status::unknown;
    
    // Generate temporary node_id from IP:port (will be updated to real ID via gossip)
    std::string temp_id = std::string(ip) + ":" + std::to_string(port);
    NodeId temp_node_id{};
    std::hash<std::string> hasher;
    uint64_t hash = hasher(temp_id);
    std::memcpy(temp_node_id.data(), &hash, sizeof(hash));
    node.id = temp_node_id;

    gossip_core_->meet(node);
    ASTRADB_LOG_INFO("Meeting node: {}:{} (temp id: {})", ip, port, NodeIdToString(node.id));
    return true;
  }

  // Join an existing cluster through a known node
  bool JoinCluster(std::string_view ip, int port) noexcept {
    if (ASTRADB_UNLIKELY(!running_.load(std::memory_order_acquire))) {
      return false;
    }

    libgossip::node_view node;
    node.ip = std::string(ip);
    node.port = port;

    gossip_core_->join(node);
    ASTRADB_LOG_INFO("Joining cluster via: {}:{}", ip, port);
    return true;
  }

  // Leave the cluster gracefully
  void LeaveCluster() noexcept {
    if (gossip_core_) {
      gossip_core_->leave(self_id_);
    }
    ASTRADB_LOG_INFO("Leaving cluster");
  }

  // Get all known nodes
  std::vector<AstraNodeView> GetNodes() const noexcept {
    std::vector<AstraNodeView> result;

    if (ASTRADB_UNLIKELY(!gossip_core_)) {
      return result;
    }

    auto nodes = gossip_core_->get_nodes();
    result.reserve(nodes.size());

    for (const auto& node : nodes) {
      result.push_back(ConvertNodeView(node));
    }

    return result;
  }

  // Get node count
  size_t GetNodeCount() const noexcept {
    return gossip_core_ ? gossip_core_->size() : 0;
  }

  // Find node by ID
  std::optional<AstraNodeView> FindNode(const NodeId& id) const noexcept {
    if (ASTRADB_UNLIKELY(!gossip_core_)) {
      return std::nullopt;
    }

    auto node = gossip_core_->find_node(id);
    if (node) {
      return ConvertNodeView(*node);
    }
    return std::nullopt;
  }

  // Get self node view
  AstraNodeView GetSelf() const noexcept {
    if (ASTRADB_UNLIKELY(!gossip_core_)) {
      return {};
    }

    return ConvertNodeView(gossip_core_->self());
  }

  // ========== Event Handling ==========

  // Set event callback
  void SetEventCallback(ClusterEventCallback callback) noexcept {
    std::lock_guard<std::mutex> lock(event_callback_mutex_);
    event_callback_ = std::move(callback);
    event_callback_set_.store(true, std::memory_order_release);
  }

  // ========== Statistics ==========

  // Get gossip statistics
  struct GossipStats {
    size_t known_nodes;
    size_t sent_messages;
    size_t received_messages;
    int64_t last_tick_duration_ms;
  };

  GossipStats GetStats() const noexcept {
    GossipStats stats{};

    if (gossip_core_) {
      auto core_stats = gossip_core_->get_stats();
      stats.known_nodes = core_stats.known_nodes;
      stats.sent_messages = core_stats.sent_messages;
      stats.received_messages = core_stats.received_messages;
      stats.last_tick_duration_ms = core_stats.last_tick_duration.count();
    }

    return stats;
  }

  // ========== Node ID Utilities ==========

  // Convert NodeId to hex string
  static std::string NodeIdToString(const NodeId& id) noexcept {
    constexpr char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(32);

    for (uint8_t byte : id) {
      result.push_back(hex_chars[byte >> 4]);
      result.push_back(hex_chars[byte & 0x0f]);
    }

    return result;
  }

  // Parse NodeId from hex string
  static bool ParseNodeId(std::string_view hex, NodeId& id) noexcept {
    if (hex.size() != 32) {
      return false;
    }

    for (size_t i = 0; i < 16; ++i) {
      uint8_t byte = 0;

      for (int j = 0; j < 2; ++j) {
        char c = hex[i * 2 + j];
        byte <<= 4;

        if (c >= '0' && c <= '9') {
          byte |= c - '0';
        } else if (c >= 'a' && c <= 'f') {
          byte |= c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
          byte |= c - 'A' + 10;
        } else {
          return false;
        }
      }

      id[i] = byte;
    }

    return true;
  }

  // Generate random NodeId
  static void GenerateNodeId(NodeId& id) noexcept {
    // Use current time and random data for ID generation
    auto now = absl::Now();
    auto nanos = absl::ToUnixNanos(now);

    // Simple hash-based generation (in production, use proper UUID or crypto
    // RNG)
    for (size_t i = 0; i < 16; ++i) {
      id[i] = static_cast<uint8_t>((nanos >> (i * 4)) ^ (i * 17));
    }
  }

 private:
  // Convert libgossip::node_view to AstraNodeView
  static AstraNodeView ConvertNodeView(
      const libgossip::node_view& node) noexcept {
    AstraNodeView view;
    view.id = node.id;
    view.ip = node.ip;
    view.port = node.port;
    view.role = node.role;
    view.region = node.region;
    view.config_epoch = node.config_epoch;
    view.heartbeat = node.heartbeat;
    view.version = node.version;
    view.status = node.status;

    // AstraDB-specific fields (would be populated from metadata)
    view.shard_count = 1;
    view.memory_used = 0;
    view.keys_count = 0;

    // Copy metadata
    for (const auto& [k, v] : node.metadata) {
      view.metadata.emplace(k, v);
    }

    return view;
  }

  // Callback: send message to target node via transport
  void OnSendMessage(const libgossip::gossip_message& msg,
                     const libgossip::node_view& target) noexcept {
    if (ASTRADB_UNLIKELY(!transport_)) {
      ASTRADB_LOG_WARN("Transport not available for sending message");
      return;
    }

    // Always log send operations
    ASTRADB_LOG_DEBUG("OnSendMessage: to {}:{}, type={}, entries={}",
                     target.ip, target.port, static_cast<int>(msg.type), msg.entries.size());

    // Log entries metadata in detail
    for (size_t i = 0; i < msg.entries.size(); ++i) {
      bool has_slots = msg.entries[i].metadata.contains("slots");
      std::string slots_info = has_slots ? msg.entries[i].metadata.at("slots") : "(none)";
      ASTRADB_LOG_DEBUG("OnSendMessage: entries[{}]: id={}, has_slots={}, slots='{}', heartbeat={}, config_epoch={}",
                       i, NodeIdToString(msg.entries[i].id), 
                       has_slots, slots_info, msg.entries[i].heartbeat, msg.entries[i].config_epoch);
    }

    // Log when sending slot metadata
    if (!msg.entries.empty() && msg.entries[0].metadata.contains("slots")) {
      ASTRADB_LOG_DEBUG("OnSendMessage: SENDING SLOT METADATA to {}:{} - slots='{}', heartbeat={}",
                       target.ip, target.port,
                       msg.entries[0].metadata.at("slots"),
                       msg.entries[0].heartbeat);
    }

    // Use libgossip's transport to send message
    auto result = transport_->send_message(msg, target);
    if (result != gossip::net::error_code::success) {
      ASTRADB_LOG_ERROR("Failed to send message to {}:{}", target.ip,
                        target.port);
    } else if (!msg.entries.empty() && msg.entries[0].metadata.contains("slots")) {
      ASTRADB_LOG_DEBUG("OnSendMessage: Slot metadata sent successfully to {}:{}", target.ip, target.port);
    }
  }

  // Callback: node status changed
  void OnNodeEvent(const libgossip::node_view& node,
                   libgossip::node_status old_status) noexcept {
    ClusterEvent event = ClusterEvent::kConfigChanged;

    // Log metadata info
    bool has_slots = node.metadata.contains("slots");
    std::string slots_info = has_slots ? node.metadata.at("slots") : "(none)";
    ASTRADB_LOG_DEBUG("OnNodeEvent: node={}, ip={}:{}, status: {} -> {}, has_slots={}, slots='{}', config_epoch={}, heartbeat={}",
                     NodeIdToString(node.id), node.ip, node.port,
                     libgossip::to_string(old_status), libgossip::to_string(node.status),
                     has_slots, slots_info, node.config_epoch, node.heartbeat);

    switch (node.status) {
      case libgossip::node_status::online:
        if (old_status == libgossip::node_status::suspect) {
          event = ClusterEvent::kNodeRecovered;
        } else if (old_status == libgossip::node_status::online) {
          // Status didn't change (both online), but notify was called
          // This means metadata changed
          event = ClusterEvent::kConfigChanged;
          ASTRADB_LOG_DEBUG("Metadata changed for node {}: slots='{}'", NodeIdToString(node.id), slots_info);
        } else {
          event = ClusterEvent::kNodeJoined;
        }
        break;
      case libgossip::node_status::failed:
        event = ClusterEvent::kNodeFailed;
        break;
      case libgossip::node_status::unknown:
        if (old_status == libgossip::node_status::online) {
          event = ClusterEvent::kNodeLeft;
        }
        break;
      default:
        break;
    }

    ASTRADB_LOG_DEBUG("OnNodeEvent: node={}, event={}, event_callback_set_={}",
                     NodeIdToString(node.id), static_cast<int>(event), event_callback_set_.load());

    // Notify event callbacks if set
    if (event_callback_set_.load(std::memory_order_acquire)) {
      ASTRADB_LOG_DEBUG("Calling event callback for event {}", static_cast<int>(event));
      AstraNodeView node_view;
      node_view.id = node.id;
      node_view.ip = node.ip;
      node_view.port = node.port;
      node_view.role = config_.role;
      node_view.region = config_.region;
      node_view.config_epoch = node.config_epoch;
      node_view.heartbeat = node.heartbeat;
      node_view.version = node.version;
      node_view.status = node.status;
      node_view.shard_count = config_.shard_count;

      // Copy metadata including slot assignments
      for (const auto& [key, value] : node.metadata) {
        node_view.metadata[key] = value;
      }

      // Check if this node has slot metadata
      if (node.metadata.contains("slots")) {
        const std::string& slots_str = node.metadata.at("slots");
        ASTRADB_LOG_DEBUG("Node {} has slot metadata ({} bytes)",
                         NodeIdToString(node.id), slots_str.size());
      }

      // Lock and call callback to prevent race with Stop()
      std::function<void(ClusterEvent, const AstraNodeView&)> callback;
      {
        std::lock_guard<std::mutex> lock(event_callback_mutex_);
        if (event_callback_set_.load(std::memory_order_acquire)) {
          callback = event_callback_;
        }
      }

      if (callback) {
        ASTRADB_LOG_DEBUG("Calling event callback: event={}, node_id={}", static_cast<int>(event), NodeIdToString(node.id));
        callback(event, node_view);
      } else {
        ASTRADB_LOG_WARN("Event callback is null, skipping cluster event notification");
      }
    } else {
      ASTRADB_LOG_WARN("Event callback not set, skipping cluster event notification");
    }
  }

  ClusterConfig config_;
  NodeId self_id_{};
  std::shared_ptr<libgossip::gossip_core> gossip_core_;
  std::unique_ptr<gossip::net::transport> transport_;

  std::atomic<bool> initialized_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> event_callback_set_{false};
  std::function<void(ClusterEvent, const AstraNodeView&)> event_callback_;
  mutable std::mutex event_callback_mutex_;  // Protect event_callback_ access

  uint64_t config_epoch_{1};  // Configuration version for conflict resolution
  std::string slot_metadata_;  // Local slot metadata storage
};

}  // namespace astra::cluster
