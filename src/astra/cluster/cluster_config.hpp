// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/strings/string_view.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "astra/base/logging.hpp"
#include "cluster_manager.hpp"  // For ClusterRole
#include "shard_manager.hpp"    // For HashSlotCalculator

namespace astra::cluster {

// Cluster node info
struct ClusterNodeInfo {
  std::string id;
  std::string ip;
  uint16_t port;
  uint16_t bus_port;
  ClusterRole role = ClusterRole::kNone;
  std::string master_id;
  uint64_t config_epoch = 0;

  // Hash function for quick lookups
  bool operator==(const ClusterNodeInfo& other) const noexcept {
    return id == other.id;
  }

  struct Hash {
    size_t operator()(const ClusterNodeInfo& node) const noexcept {
      return std::hash<std::string>{}(node.id);
    }
  };
};

// Immutable cluster state
// Following Dragonfly's pattern: thread-local shared_ptr for zero-copy updates
class ClusterState {
 public:
  using NodeMap = absl::flat_hash_map<std::string, ClusterNodeInfo>;
  using SlotMap = std::array<std::string, 16384>;  // slot -> node_id

  ClusterState() noexcept = default;

  // Create cluster state with self node
  explicit ClusterState(const std::string& self_id, const std::string& ip,
                         uint16_t port, uint16_t bus_port) noexcept {
    ClusterNodeInfo self;
    self.id = self_id;
    self.ip = ip;
    self.port = port;
    self.bus_port = bus_port;
    self.role = ClusterRole::kMaster;
    self.config_epoch = absl::GetCurrentTimeNanos() / 1000000;

    nodes_[self_id] = self;
    self_id_ = self_id;
    enabled_ = true;
  }

  // Get self node ID
  const std::string& GetSelfId() const noexcept { return self_id_; }

  // Check if cluster is enabled
  bool IsEnabled() const noexcept { return enabled_; }

  // Get all nodes
  const NodeMap& GetNodes() const noexcept { return nodes_; }

  // Get node by ID
  std::optional<ClusterNodeInfo> GetNode(
      const std::string& node_id) const noexcept {
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  // Get slot owner
  std::optional<std::string> GetSlotOwner(uint16_t slot) const noexcept {
    if (slot >= 16384 || slot_owners_[slot].empty()) {
      return std::nullopt;
    }
    return slot_owners_[slot];
  }

  // Create a new state with added node
  std::shared_ptr<ClusterState> WithNodeAdded(
      const ClusterNodeInfo& node) const noexcept {
    auto new_state = std::make_shared<ClusterState>(*this);
    new_state->nodes_[node.id] = node;
    return new_state;
  }

  // Create a new state with removed node
  std::shared_ptr<ClusterState> WithNodeRemoved(
      const std::string& node_id) const noexcept {
    auto new_state = std::make_shared<ClusterState>(*this);
    new_state->nodes_.erase(node_id);
    return new_state;
  }

  // Create a new state with assigned slots
  std::shared_ptr<ClusterState> WithSlotsAssigned(
      const std::string& node_id,
      const std::vector<uint16_t>& slots) const noexcept {
    auto new_state = std::make_shared<ClusterState>(*this);
    for (uint16_t slot : slots) {
      if (slot < 16384) {
        new_state->slot_owners_[slot] = node_id;
      }
    }
    return new_state;
  }

  // Create a new state with removed slots
  std::shared_ptr<ClusterState> WithSlotsRemoved(
      const std::vector<uint16_t>& slots) const noexcept {
    auto new_state = std::make_shared<ClusterState>(*this);
    for (uint16_t slot : slots) {
      if (slot < 16384) {
        new_state->slot_owners_[slot].clear();
      }
    }
    return new_state;
  }

 private:
  NodeMap nodes_;
  SlotMap slot_owners_{};  // Initialize with empty strings
  std::string self_id_;
  bool enabled_ = false;
};

// Thread-local cluster state accessor
// Following Dragonfly's pattern: each Worker has its own state snapshot
class ClusterStateAccessor {
 public:
  // Get thread-local cluster state
  static ClusterState* Get() noexcept { return state_; }

  // Set thread-local cluster state (zero-copy update)
  static void Set(std::shared_ptr<ClusterState> state) noexcept {
    state_ptr_ = std::move(state);
    state_ = state_ptr_.get();
  }

  // Get slot owner for a key
  static std::optional<std::string> GetSlotOwner(const std::string& key) noexcept {
    auto* state = Get();
    if (!state || !state->IsEnabled()) {
      return std::nullopt;
    }

    uint16_t slot = HashSlotCalculator::CalculateWithTag(key);
    return state->GetSlotOwner(slot);
  }

  // Check if key should be redirected
  static std::optional<std::string> CheckKeyRedirect(
      const std::string& key) noexcept {
    auto* state = Get();
    if (!state || !state->IsEnabled()) {
      return std::nullopt;  // No cluster mode, process locally
    }

    uint16_t slot = HashSlotCalculator::CalculateWithTag(key);
    auto owner = state->GetSlotOwner(slot);

    if (!owner) {
      return std::nullopt;  // Slot not assigned, process locally
    }

    // If slot is owned by another node, return target node ID
    if (owner != state->GetSelfId()) {
      return owner;
    }

    return std::nullopt;  // Process locally
  }

 private:
  // Thread-local state pointer and shared_ptr to keep state alive
  static thread_local std::shared_ptr<ClusterState> state_ptr_;
  static thread_local ClusterState* state_;
};

}  // namespace astra::cluster