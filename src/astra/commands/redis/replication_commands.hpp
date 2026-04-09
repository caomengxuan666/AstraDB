// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "astra/commands/command_handler.hpp"
#include "astra/protocol/resp/resp_types.hpp"
#include "astra/replication/replication_manager.hpp"

namespace astra::commands {

// SYNC - Synchronize with master (slave only)
CommandResult HandleSync(const protocol::Command& command,
                         CommandContext* context);

// PSYNC - Partial synchronization
CommandResult HandlePsync(const protocol::Command& command,
                          CommandContext* context);

// REPLCONF - Configure replication
CommandResult HandleReplconf(const protocol::Command& command,
                             CommandContext* context);

// REPLICAOF - Set up replication
CommandResult HandleReplicaof(const protocol::Command& command,
                              CommandContext* context);

// ROLE - Return role and replication state
CommandResult HandleRole(const protocol::Command& command,
                         CommandContext* context);

}  // namespace astra::commands
