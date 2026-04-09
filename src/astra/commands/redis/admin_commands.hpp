// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "command_handler.hpp"

namespace astra::commands {

// PING
CommandResult HandlePing(const astra::protocol::Command& command,
                         CommandContext* context);

// INFO
CommandResult HandleInfo(const astra::protocol::Command& command,
                         CommandContext* context);

// COMMAND - Redis command introspection
CommandResult HandleCommand(const astra::protocol::Command& command,
                            CommandContext* context);

// DEBUG - Debug commands
CommandResult HandleDebug(const astra::protocol::Command& command,
                          CommandContext* context);

// MIGRATE - Migrate key to another node
CommandResult HandleMigrate(const astra::protocol::Command& command,
                            CommandContext* context);

// MODULE - Module management commands
CommandResult HandleModule(const astra::protocol::Command& command,
                           CommandContext* context);

// CONFIG - Configuration management commands
CommandResult HandleConfig(const astra::protocol::Command& command,
                           CommandContext* context);

// SCAN - Incrementally iterate the keyspace
CommandResult HandleScan(const astra::protocol::Command& command,
                         CommandContext* context);

// MEMORY - Memory introspection commands
CommandResult HandleMemory(const astra::protocol::Command& command,
                           CommandContext* context);

// ASKING - Indicate client is asking for a key during migration
CommandResult HandleAsking(const astra::protocol::Command& command,
                           CommandContext* context);

// BGSAVE - Background save (persistence)
CommandResult HandleBgSave(const astra::protocol::Command& command,
                           CommandContext* context);

// LASTSAVE - Last save timestamp
CommandResult HandleLastSave(const astra::protocol::Command& command,
                             CommandContext* context);

// SAVE - Synchronous save (persistence)
CommandResult HandleSave(const astra::protocol::Command& command,
                         CommandContext* context);

// DBSIZE - Return number of keys in current database
CommandResult HandleDbSize(const astra::protocol::Command& command,
                           CommandContext* context);

// FLUSHDB - Clear current database
CommandResult HandleFlushDb(const astra::protocol::Command& command,
                            CommandContext* context);

// FLUSHALL - Clear all databases
CommandResult HandleFlushAll(const astra::protocol::Command& command,
                             CommandContext* context);

// SELECT - Select database by index
CommandResult HandleSelect(const astra::protocol::Command& command,
                           CommandContext* context);

// Admin commands are auto-registered via ASTRADB_REGISTER_COMMAND macro

}  // namespace astra::commands
