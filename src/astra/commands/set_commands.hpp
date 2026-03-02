// ==============================================================================
// Set Commands Interface
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include "command_handler.hpp"

namespace astra::commands {

// SADD key member [member ...]
CommandResult HandleSAdd(const astra::protocol::Command& command, CommandContext* context);

// SREM key member [member ...]
CommandResult HandleSRem(const astra::protocol::Command& command, CommandContext* context);

// SMEMBERS key
CommandResult HandleSMembers(const astra::protocol::Command& command, CommandContext* context);

// SISMEMBER key member
CommandResult HandleSIsMember(const astra::protocol::Command& command, CommandContext* context);

// SCARD key
CommandResult HandleSCard(const astra::protocol::Command& command, CommandContext* context);

// Set commands are auto-registered via ASTRADB_REGISTER_COMMAND macro

}  // namespace astra::commands