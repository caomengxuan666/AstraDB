// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "command_handler.hpp"
#include "database.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace astra::commands {

// Client Management Operations

// CLIENT LIST - List all connected clients
CommandResult HandleClientList(const protocol::Command& command, CommandContext* context);

// CLIENT INFO - Get information about the current client connection
CommandResult HandleClientInfo(const protocol::Command& command, CommandContext* context);

// CLIENT KILL [ip:port] [ID client-id] [TYPE normal|master|slave|pubsub] - Kill a client connection
CommandResult HandleClientKill(const protocol::Command& command, CommandContext* context);

}  // namespace astra::commands