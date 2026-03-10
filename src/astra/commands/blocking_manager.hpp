// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/container/flat_hash_map.h>

#include <asio/steady_timer.hpp>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

#include "astra/protocol/resp/resp_types.hpp"

namespace astra {

namespace network {
class Connection;
}  // namespace network

}  // namespace astra

namespace astra::commands {

// Blocked client structure
struct BlockedClient {
  uint64_t client_id;
  std::string key;
  astra::protocol::Command command;
  double timeout_seconds;
  std::chrono::steady_clock::time_point start_time;
  std::function<astra::protocol::RespValue(const astra::protocol::RespValue&)>
      callback;  // Returns result to send
  astra::network::Connection* connection =
      nullptr;  // Save connection pointer for async response
};

// Blocking manager for Redis-style blocking commands (BLPOP, BRPOP, BZPOPMIN,
// etc.)
class BlockingManager {
 public:
  explicit BlockingManager(asio::io_context& io_context);
  ~BlockingManager() = default;

  // Add a client to the wait queue
  void AddBlockedClient(const std::string& key, BlockedClient client);

  // Remove a client from all wait queues
  void RemoveBlockedClient(uint64_t client_id);

  // Wake up clients waiting on a specific key
  void WakeUpBlockedClients(const std::string& key);

  // Clean up expired requests (called periodically)
  void CleanExpiredRequests();

  // Get number of blocked clients
  size_t GetBlockedClientCount() const;

  // Check if a client is blocked
  bool IsClientBlocked(uint64_t client_id) const;

 private:
  void HandleTimeout(uint64_t client_id);
  void ProcessBlockedCommand(const BlockedClient& client);

  asio::io_context& io_context_;

  // Per-key wait queue
  std::unordered_map<std::string, std::deque<BlockedClient>> wait_queues_;
  mutable std::shared_mutex wait_queues_mutex_;

  // Timeout timers
  std::unordered_map<uint64_t, std::unique_ptr<asio::steady_timer>>
      timeout_timers_;
  mutable std::shared_mutex timers_mutex_;
};

}  // namespace astra::commands
