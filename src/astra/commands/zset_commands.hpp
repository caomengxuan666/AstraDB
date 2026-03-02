// ==============================================================================
// ZSet Commands Interface
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include "command_handler.hpp"

namespace astra::commands {

// ZADD key score member [score member ...]
CommandResult HandleZAdd(const astra::protocol::Command& command, CommandContext* context);

// ZRANGE key start stop [WITHSCORES]
CommandResult HandleZRange(const astra::protocol::Command& command, CommandContext* context);

// ZREM key member [member ...]
CommandResult HandleZRem(const astra::protocol::Command& command, CommandContext* context);

// ZSCORE key member
CommandResult HandleZScore(const astra::protocol::Command& command, CommandContext* context);

// ZCARD key
CommandResult HandleZCard(const astra::protocol::Command& command, CommandContext* context);

// ZCOUNT key min max
CommandResult HandleZCount(const astra::protocol::Command& command, CommandContext* context);

// ZSet commands are auto-registered via ASTRADB_REGISTER_COMMAND macro

}  // namespace astra::commands