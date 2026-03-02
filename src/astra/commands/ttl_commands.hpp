// ==============================================================================
// TTL Commands Interface
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include "command_handler.hpp"
#include "astra/protocol/resp/resp_types.hpp"

namespace astra::commands {

// EXPIRE key seconds
CommandResult HandleExpire(const astra::protocol::Command& command, CommandContext* context);

// EXPIREAT key timestamp
CommandResult HandleExpireAt(const astra::protocol::Command& command, CommandContext* context);

// PEXPIRE key milliseconds
CommandResult HandlePExpire(const astra::protocol::Command& command, CommandContext* context);

// PEXPIREAT key timestamp_ms
CommandResult HandlePExpireAt(const astra::protocol::Command& command, CommandContext* context);

// TTL key - returns seconds until expiration
CommandResult HandleTTL(const astra::protocol::Command& command, CommandContext* context);

// PTTL key - returns milliseconds until expiration
CommandResult HandlePTTL(const astra::protocol::Command& command, CommandContext* context);

// PERSIST key - remove expiration
CommandResult HandlePersist(const astra::protocol::Command& command, CommandContext* context);

// TTL commands are auto-registered via ASTRADB_REGISTER_COMMAND macro

}  // namespace astra::commands