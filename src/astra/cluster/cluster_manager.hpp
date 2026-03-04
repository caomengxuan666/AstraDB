// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <atomic>

#include "astra/base/logging.hpp"

namespace astra::cluster {

// Node role in cluster
enum class ClusterRole {
  kMaster,
  kSlave,
  kNone
};

// Cluster node info
struct ClusterNode {
  std::string id;
  std::string ip;
  uint16_t port;
  uint16_t bus_port;
  uint32_t flags = 0;
  ClusterRole role = ClusterRole::kNone;
  std::string master_id;
  uint64_t ping_sent = 0;
  uint64_t pong_recv = 0;
  uint64_t config_epoch = 0;
  uint8_t slots[16384] = {0};
};

// Cluster manager
class ClusterManager {
 public:
  ClusterManager() noexcept = default;
  ~ClusterManager() noexcept = default;
  
  // Non-copyable, non-movable
  ClusterManager(const ClusterManager&) = delete;
  ClusterManager& operator=(const ClusterManager&) = delete;
  ClusterManager(bool) = delete;
  ClusterManager& operator=(bool) = delete;
  
  // Initialize cluster
  bool Init(const std::string& node_id, const std::string& ip, 
            uint16_t port, uint16_t bus_port) noexcept {
    node_id_ = node_id;
    ip_ = ip;
    port_ = port;
    bus_port_ = bus_port;
    
    // Add self to cluster
    ClusterNode self;
    self.id = node_id;
    self.ip = ip;
    self.port = port;
    self.bus_port = bus_port;
    self.role = ClusterRole::kMaster;
    self.config_epoch = absl::GetCurrentTimeNanos() / 1000000;
    
    nodes_[node_id] = self;
    enabled_.store(true, std::memory_order_release);
    
    ASTRADB_LOG_INFO("Cluster initialized: {}:{}:{} (ID: {})", ip, port, bus_port, node_id);
    return true;
  }
  
  // Check if cluster is enabled
  bool IsEnabled() const noexcept {
    return enabled_.load(std::memory_order_acquire);
  }
  
  // Get node ID
  const std::string& GetNodeId() const noexcept { return node_id_; }
  
  // Get cluster info
  std::vector<ClusterNode> GetNodes() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ClusterNode> nodes;
    for (const auto& [id, node] : nodes_) {
      nodes.push_back(node);
    }
    return nodes;
  }
  
  // Add node to cluster
  bool AddNode(const ClusterNode& node) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (nodes_.find(node.id) != nodes_.end()) {
      return false;
    }
    nodes_[node.id] = node;
    ASTRADB_LOG_INFO("Added node to cluster: {}:{} (ID: {})", node.ip, node.port, node.id);
    return true;
  }
  
  // Remove node from cluster
  bool RemoveNode(const std::string& node_id) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
      return false;
    }
    nodes_.erase(it);
    ASTRADB_LOG_INFO("Removed node from cluster: {}", node_id);
    return true;
  }
  
  // Get node by ID
  std::optional<ClusterNode> GetNode(const std::string& node_id) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
      return std::nullopt;
    }
    return it->second;
  }
  
  // Assign slots to node
  bool AssignSlots(const std::string& node_id, const std::vector<uint16_t>& slots) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
      return false;
    }
    
    // Clear previous slots
    memset(it->second.slots, 0, sizeof(it->second.slots));
    
    // Assign new slots
    for (uint16_t slot : slots) {
      if (slot < 16384) {
        it->second.slots[slot] = 1;
      }
    }
    
    return true;
  }
  
  // Get slot owner
  std::optional<std::string> GetSlotOwner(uint16_t slot) const noexcept {
    if (slot >= 16384) {
      return std::nullopt;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, node] : nodes_) {
      if (node.slots[slot]) {
        return id;
      }
    }
    return std::nullopt;
  }
  
 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, ClusterNode> nodes_;
  std::string node_id_;
  std::string ip_;
  uint16_t port_ = 0;
  uint16_t bus_port_ = 0;
  std::atomic<bool> enabled_{false};
};

}  // namespace astra::cluster