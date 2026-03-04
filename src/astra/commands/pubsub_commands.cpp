// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "pubsub_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/base/logging.hpp"
#include <absl/strings/ascii.h>

namespace astra::commands {

CommandResult HandleSubscribe(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'subscribe' command");
  }

  auto* manager = context->GetPubSubManager();
  if (!manager) {
    return CommandResult(false, "ERR pub/sub not available");
  }

  // Collect channels
  std::vector<std::string> channels;
  channels.reserve(command.ArgCount());
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    if (command[i].IsBulkString()) {
      channels.push_back(command[i].AsString());
    }
  }

  // Subscribe
  manager->Subscribe(context->GetConnectionId(), channels);

  // Response is sent asynchronously by the manager
  // Return empty success to indicate command was processed
  return CommandResult();
}

CommandResult HandleUnsubscribe(const protocol::Command& command, CommandContext* context) {
  auto* manager = context->GetPubSubManager();
  if (!manager) {
    return CommandResult(false, "ERR pub/sub not available");
  }

  // Collect channels (optional - if empty, unsubscribe from all)
  std::vector<std::string> channels;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    if (command[i].IsBulkString()) {
      channels.push_back(command[i].AsString());
    }
  }

  // Unsubscribe
  manager->Unsubscribe(context->GetConnectionId(), channels);

  return CommandResult();
}

CommandResult HandlePublish(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'publish' command");
  }

  auto* manager = context->GetPubSubManager();
  if (!manager) {
    return CommandResult(false, "ERR pub/sub not available");
  }

  const std::string& channel = command[0].AsString();
  const std::string& message = command[1].AsString();

  // Publish and return subscriber count
  size_t subscribers = manager->Publish(channel, message);

  protocol::RespValue response;
  response.SetInteger(static_cast<int64_t>(subscribers));
  return CommandResult(response);
}

CommandResult HandlePSubscribe(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'psubscribe' command");
  }

  auto* manager = context->GetPubSubManager();
  if (!manager) {
    return CommandResult(false, "ERR pub/sub not available");
  }

  // Collect patterns
  std::vector<std::string> patterns;
  patterns.reserve(command.ArgCount());
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    if (command[i].IsBulkString()) {
      patterns.push_back(command[i].AsString());
    }
  }

  // Subscribe to patterns
  manager->PSubscribe(context->GetConnectionId(), patterns);

  return CommandResult();
}

CommandResult HandlePUnsubscribe(const protocol::Command& command, CommandContext* context) {
  auto* manager = context->GetPubSubManager();
  if (!manager) {
    return CommandResult(false, "ERR pub/sub not available");
  }

  // Collect patterns (optional)
  std::vector<std::string> patterns;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    if (command[i].IsBulkString()) {
      patterns.push_back(command[i].AsString());
    }
  }

  // Unsubscribe from patterns
  manager->PUnsubscribe(context->GetConnectionId(), patterns);

  return CommandResult();
}

CommandResult HandlePubSub(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'pubsub' command");
  }

  const std::string& subcommand = command[0].AsString();
  std::string subcmd = absl::AsciiStrToLower(subcommand);

  auto* manager = context->GetPubSubManager();
  if (!manager) {
    return CommandResult(false, "ERR pub/sub not available");
  }

  if (subcmd == "channels") {
    // PUBSUB CHANNELS [pattern] - list active channels
    // TODO: Implement channel listing
    protocol::RespValue response;
    response.SetArray({});
    return CommandResult(response);
  } else if (subcmd == "numsub") {
    // PUBSUB NUMSUB [channel ...] - return subscriber counts
    std::vector<protocol::RespValue> result;
    for (size_t i = 1; i < command.ArgCount(); ++i) {
      const std::string& ch = command[i].AsString();
      protocol::RespValue channel_name;
      channel_name.SetString(ch, protocol::RespType::kBulkString);
      result.push_back(channel_name);

      protocol::RespValue count;
      count.SetInteger(0);  // TODO: Track per-channel subscriber counts
      result.push_back(count);
    }

    protocol::RespValue response;
    response.SetArray(std::move(result));
    return CommandResult(response);
  } else if (subcmd == "numpat") {
    // PUBSUB NUMPAT - return pattern subscription count
    protocol::RespValue response;
    response.SetInteger(0);  // TODO: Implement pattern count
    return CommandResult(response);
  } else {
    return CommandResult(false, "ERR unknown subcommand for PUBSUB");
  }
}

// Register Pub/Sub commands
ASTRADB_REGISTER_COMMAND(SUBSCRIBE, -2, "pubsub", RoutingStrategy::kNone, HandleSubscribe);
ASTRADB_REGISTER_COMMAND(UNSUBSCRIBE, -1, "pubsub", RoutingStrategy::kNone, HandleUnsubscribe);
ASTRADB_REGISTER_COMMAND(PUBLISH, 3, "pubsub", RoutingStrategy::kNone, HandlePublish);
ASTRADB_REGISTER_COMMAND(PSUBSCRIBE, -2, "pubsub", RoutingStrategy::kNone, HandlePSubscribe);
ASTRADB_REGISTER_COMMAND(PUNSUBSCRIBE, -1, "pubsub", RoutingStrategy::kNone, HandlePUnsubscribe);
ASTRADB_REGISTER_COMMAND(PUBSUB, -2, "pubsub", RoutingStrategy::kNone, HandlePubSub);

}  // namespace astra::commands
