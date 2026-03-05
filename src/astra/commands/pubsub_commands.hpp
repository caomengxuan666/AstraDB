// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "astra/protocol/resp/resp_types.hpp"
#include "command_handler.hpp"
#include "database.hpp"

namespace astra::commands {

// Pub/Sub manager interface - implemented by server
class PubSubManager {
 public:
  virtual ~PubSubManager() = default;

  // Subscribe to channels
  virtual void Subscribe(uint64_t conn_id, const std::vector<std::string>& channels) = 0;

  // Unsubscribe from channels (empty = all channels)
  virtual void Unsubscribe(uint64_t conn_id, const std::vector<std::string>& channels) = 0;

  // Subscribe to patterns
  virtual void PSubscribe(uint64_t conn_id, const std::vector<std::string>& patterns) = 0;

  // Unsubscribe from patterns (empty = all patterns)
  virtual void PUnsubscribe(uint64_t conn_id, const std::vector<std::string>& patterns) = 0;

  // Publish a message to a channel, returns number of subscribers
  virtual size_t Publish(const std::string& channel, const std::string& message) = 0;

  // Get subscription count for a connection
  virtual size_t GetSubscriptionCount(uint64_t conn_id) const = 0;

  // Check if connection is in pub/sub mode
  virtual bool IsSubscribed(uint64_t conn_id) const = 0;
  
  // Get list of active channels matching pattern
  virtual std::vector<std::string> GetActiveChannels(const std::string& pattern = "") const = 0;
  
  // Get subscriber count for a specific channel
  virtual size_t GetChannelSubscriberCount(const std::string& channel) const = 0;
  
  // Get pattern subscription count
  virtual size_t GetPatternSubscriptionCount() const = 0;
};

// SUBSCRIBE - Subscribe to channels
CommandResult HandleSubscribe(const protocol::Command& command, CommandContext* context);

// UNSUBSCRIBE - Unsubscribe from channels
CommandResult HandleUnsubscribe(const protocol::Command& command, CommandContext* context);

// PUBLISH - Publish a message to a channel
CommandResult HandlePublish(const protocol::Command& command, CommandContext* context);

// PSUBSCRIBE - Subscribe to patterns
CommandResult HandlePSubscribe(const protocol::Command& command, CommandContext* context);

// PUNSUBSCRIBE - Unsubscribe from patterns
CommandResult HandlePUnsubscribe(const protocol::Command& command, CommandContext* context);

// PUBSUB - Pub/Sub introspection
CommandResult HandlePubSub(const protocol::Command& command, CommandContext* context);

}  // namespace astra::commands
