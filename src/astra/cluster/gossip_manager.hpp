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

#include "core/gossip_core.hpp"
#include "net/transport_factory.hpp"
#include "net/json_serializer.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/strings/string_view.h>
#include <absl/synchronization/mutex.h>
#include <absl/time/time.h>

#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <atomic>

#include "astra/base/macros.hpp"
#include "astra/base/logging.hpp"

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
  std::string role;         // "master", "replica"
  std::string region;       // Geographic region
  uint64_t config_epoch;    // Configuration version
  uint64_t heartbeat;       // Logical heartbeat
  uint64_t version;         // Data version
  NodeStatus status;
  
  // AstraDB-specific fields
  uint32_t shard_count;     // Number of shards this node manages
  uint64_t memory_used;     // Memory usage in bytes
  uint64_t keys_count;      // Total key count
  
  // Metadata
  absl::flat_hash_map<std::string, std::string> metadata;
};

// Cluster configuration
struct ClusterConfig {
  std::string node_id;          // Unique node ID (hex string)
  std::string bind_ip = "0.0.0.0";
  int gossip_port = 7379;       // Gossip protocol port
  int data_port = 6379;         // Data service port
  
  // Timing configuration
  int heartbeat_interval_ms = 100;    // Heartbeat interval
  int failure_timeout_ms = 2000;      // Failure detection timeout
  int cleanup_timeout_ms = 60000;     // Cleanup expired nodes timeout
  
  // Gossip configuration
  int gossip_nodes = 3;         // Number of nodes to gossip with per tick
  int sync_nodes = 2;           // Number of nodes to sync per message
  
  // Transport type
  bool use_tcp = false;         // false = UDP, true = TCP
  
  // Cluster settings
  std::string region;           // Geographic region
  std::string role = "master";  // "master" or "replica"
  uint32_t shard_count = 1;     // Number of shards on this node
};

// Cluster event types
enum class ClusterEvent : uint8_t {
  kNodeJoined,       // New node joined
  kNodeLeft,         // Node left gracefully
  kNodeFailed,       // Node failed
  kNodeRecovered,    // Node recovered from suspect
  kLeaderChanged,    // Leader changed
  kConfigChanged,    // Cluster config changed
};

// Event callback type
using ClusterEventCallback = absl::FunctionRef<void(ClusterEvent, const AstraNodeView&)>;

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

  // Movable
  GossipManager(GossipManager&&) noexcept = default;
  GossipManager& operator=(GossipManager&&) noexcept = default;

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
    
    // Create gossip core with callbacks
    try {
      gossip_core_ = std::make_shared<libgossip::gossip_core>(
          self_view,
          [this](const libgossip::gossip_message& msg, const libgossip::node_view& target) {
            OnSendMessage(msg, target);
          },
          [this](const libgossip::node_view& node, libgossip::node_status old_status) {
            OnNodeEvent(node, old_status);
          }
      );
    } catch (const std::exception& e) {
      ASTRADB_LOG_ERROR("Failed to create gossip core: {}", e.what());
      return false;
    }
    
    // Create transport using libgossip's transport factory
    auto transport_type = config.use_tcp 
        ? gossip::net::transport_type::tcp 
        : gossip::net::transport_type::udp;
    
    transport_ = gossip::net::transport_factory::create_transport(
        transport_type, config.bind_ip, static_cast<uint16_t>(config.gossip_port));
    
    if (!transport_) {
      ASTRADB_LOG_ERROR("Failed to create transport on {}:{}", 
                        config.bind_ip, config.gossip_port);
      return false;
    }
    
    // Create JSON serializer
    auto serializer = std::make_unique<gossip::net::json_serializer>();
    transport_->set_serializer(std::move(serializer));
    
    // Set gossip core for transport (for receiving messages)
    transport_->set_gossip_core(gossip_core_);
    
    initialized_.store(true, std::memory_order_release);
    ASTRADB_LOG_INFO("GossipManager initialized: node_id={}, ip={}:{}", 
                     NodeIdToString(self_id_), config.bind_ip, config.gossip_port);
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
    
    gossip_core_->meet(node);
    ASTRADB_LOG_INFO("Meeting node: {}:{}", ip, port);
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
    // Note: In production, we'd store this properly
    // For now, we use the libgossip event callback
    (void)callback;
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
    
    // Simple hash-based generation (in production, use proper UUID or crypto RNG)
    for (size_t i = 0; i < 16; ++i) {
      id[i] = static_cast<uint8_t>((nanos >> (i * 4)) ^ (i * 17));
    }
  }

 private:
  // Convert libgossip::node_view to AstraNodeView
  static AstraNodeView ConvertNodeView(const libgossip::node_view& node) noexcept {
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
    
    // Use libgossip's transport to send message
    auto result = transport_->send_message(msg, target);
    if (result != gossip::net::error_code::success) {
      ASTRADB_LOG_DEBUG("Failed to send message to {}:{}", target.ip, target.port);
    }
  }

  // Callback: node status changed
  void OnNodeEvent(const libgossip::node_view& node,
                   libgossip::node_status old_status) noexcept {
    ClusterEvent event = ClusterEvent::kConfigChanged;
    
    switch (node.status) {
      case libgossip::node_status::online:
        if (old_status == libgossip::node_status::suspect) {
          event = ClusterEvent::kNodeRecovered;
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
    
    ASTRADB_LOG_INFO("Node event: {} (status: {} -> {})", 
                     NodeIdToString(node.id),
                     libgossip::to_string(old_status),
                     libgossip::to_string(node.status));
    
    (void)event;  // TODO: Notify event callbacks
  }

  ClusterConfig config_;
  NodeId self_id_{};
  std::shared_ptr<libgossip::gossip_core> gossip_core_;
  std::unique_ptr<gossip::net::transport> transport_;
  
  std::atomic<bool> initialized_{false};
  std::atomic<bool> running_{false};
};

}  // namespace astra::cluster