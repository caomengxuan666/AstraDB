// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "command_handler.hpp"

namespace astra::commands {

// PING
CommandResult HandlePing(const astra::protocol::Command& command, CommandContext* context);

// INFO
CommandResult HandleInfo(const astra::protocol::Command& command, CommandContext* context);

// COMMAND - Redis command introspection
CommandResult HandleCommand(const astra::protocol::Command& command, CommandContext* context);

// DEBUG - Debug commands
CommandResult HandleDebug(const astra::protocol::Command& command, CommandContext* context);

// CLUSTER - Cluster management commands
CommandResult HandleCluster(const astra::protocol::Command& command, CommandContext* context);

// BGSAVE - Background save (persistence)
CommandResult HandleBgSave(const astra::protocol::Command& command, CommandContext* context);

// LASTSAVE - Last save timestamp
CommandResult HandleLastSave(const astra::protocol::Command& command, CommandContext* context);

// SAVE - Synchronous save (persistence)
CommandResult HandleSave(const astra::protocol::Command& command, CommandContext* context);

// Admin commands are auto-registered via ASTRADB_REGISTER_COMMAND macro

}  // namespace astra::commands