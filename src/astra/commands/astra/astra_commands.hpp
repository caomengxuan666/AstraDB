#pragma once

#include <string>
#include <vector>

#include "astra/commands/command_handler.hpp"
#include "astra/protocol/resp/resp_types.hpp"

namespace astra::commands {

/**
 * @brief AstraDB-specific command handlers
 *
 * These commands are not part of the standard Redis protocol and
 * provide AstraDB-specific functionality for debugging, management,
 * and performance monitoring.
 *
 * Astra commands follow the naming convention: ASTRADB <command>
 */

// Storage mode management
CommandResult HandleAstraStorageMode(const protocol::Command& command,
                                     CommandContext* context);

// RocksDB-specific operations
CommandResult HandleAstraRocksdbInfo(const protocol::Command& command,
                                     CommandContext* context);
CommandResult HandleAstraRocksdbCompact(const protocol::Command& command,
                                        CommandContext* context);
CommandResult HandleAstraRocksdbStats(const protocol::Command& command,
                                      CommandContext* context);

// Performance monitoring
CommandResult HandleAstraPerfStats(const protocol::Command& command,
                                   CommandContext* context);
CommandResult HandleAstraMemoryMap(const protocol::Command& command,
                                   CommandContext* context);

// Storage mode migration
CommandResult HandleAstraMigrateToRocksdb(const protocol::Command& command,
                                          CommandContext* context);
CommandResult HandleAstraMigrateToRedis(const protocol::Command& command,
                                        CommandContext* context);

}  // namespace astra::commands
