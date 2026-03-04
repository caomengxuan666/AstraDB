// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "astra/protocol/resp/resp_types.hpp"
#include "command_handler.hpp"
#include "database.hpp"

namespace astra::commands {

// Transaction state per connection
struct TransactionState {
  bool in_transaction = false;
  absl::InlinedVector<protocol::Command, 16> queued_commands;
  absl::flat_hash_set<std::string> watched_keys;
  absl::flat_hash_map<std::string, uint64_t> watched_key_versions;
};

// MULTI - Start a transaction
CommandResult HandleMulti(const protocol::Command& command, CommandContext* context);

// EXEC - Execute all commands in the transaction
CommandResult HandleExec(const protocol::Command& command, CommandContext* context);

// DISCARD - Discard the current transaction
CommandResult HandleDiscard(const protocol::Command& command, CommandContext* context);

// WATCH - Watch keys for changes
CommandResult HandleWatch(const protocol::Command& command, CommandContext* context);

// UNWATCH - Unwatch all keys
CommandResult HandleUnwatch(const protocol::Command& command, CommandContext* context);

}  // namespace astra::commands
