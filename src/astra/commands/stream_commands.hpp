// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "astra/protocol/resp/resp_types.hpp"
#include "command_handler.hpp"

namespace astra::commands {

// XADD - Add entry to stream
CommandResult HandleXAdd(const protocol::Command& command,
                         CommandContext* context);

// XREAD - Read from stream(s)
CommandResult HandleXRead(const protocol::Command& command,
                          CommandContext* context);

// XRANGE - Get range of entries
CommandResult HandleXRange(const protocol::Command& command,
                           CommandContext* context);

// XLEN - Get stream length
CommandResult HandleXLen(const protocol::Command& command,
                         CommandContext* context);

// XDEL - Delete entry
CommandResult HandleXDel(const protocol::Command& command,
                         CommandContext* context);

// XTRIM - Trim stream to length
CommandResult HandleXTrim(const protocol::Command& command,
                          CommandContext* context);

// XGROUP - Manage consumer groups
CommandResult HandleXGroup(const protocol::Command& command,
                           CommandContext* context);

// XREADGROUP - Read from consumer group
CommandResult HandleXReadGroup(const protocol::Command& command,
                               CommandContext* context);

// XACK - Acknowledge message
CommandResult HandleXAck(const protocol::Command& command,
                         CommandContext* context);

// XINFO - Stream information
CommandResult HandleXInfo(const protocol::Command& command,
                          CommandContext* context);

}  // namespace astra::commands
