// ==============================================================================
// Hash Commands Interface
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include "command_handler.hpp"

namespace astra::commands {

// HSET key field value
CommandResult HandleHSet(const astra::protocol::Command& command,
                         CommandContext* context);

// HGET key field
CommandResult HandleHGet(const astra::protocol::Command& command,
                         CommandContext* context);

// HDEL key field [field ...]
CommandResult HandleHDel(const astra::protocol::Command& command,
                         CommandContext* context);

// HEXISTS key field
CommandResult HandleHExists(const astra::protocol::Command& command,
                            CommandContext* context);

// HGETALL key
CommandResult HandleHGetAll(const astra::protocol::Command& command,
                            CommandContext* context);

// HLEN key
CommandResult HandleHLen(const astra::protocol::Command& command,
                         CommandContext* context);

// Hash commands are auto-registered via ASTRADB_REGISTER_COMMAND macro

}  // namespace astra::commands
