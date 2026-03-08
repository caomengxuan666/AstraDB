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

// SMOVE source destination member
CommandResult HandleSMove(const astra::protocol::Command& command, CommandContext* context);

// SINTER key [key ...]
CommandResult HandleSInter(const astra::protocol::Command& command, CommandContext* context);

// SUNION key [key ...]
CommandResult HandleSUnion(const astra::protocol::Command& command, CommandContext* context);

// SDIFF key [key ...]
CommandResult HandleSDiff(const astra::protocol::Command& command, CommandContext* context);

// SINTERSTORE destination key [key ...]
CommandResult HandleSInterStore(const astra::protocol::Command& command, CommandContext* context);

// SUNIONSTORE destination key [key ...]
CommandResult HandleSUnionStore(const astra::protocol::Command& command, CommandContext* context);

// SDIFFSTORE destination key [key ...]
CommandResult HandleSDiffStore(const astra::protocol::Command& command, CommandContext* context);

// SPOP key [count]
CommandResult HandleSPop(const astra::protocol::Command& command, CommandContext* context);

// SRANDMEMBER key [count]
CommandResult HandleSRandMember(const astra::protocol::Command& command, CommandContext* context);

// Set commands are auto-registered via ASTRADB_REGISTER_COMMAND macro

}  // namespace astra::commands