// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "command_handler.hpp"

namespace astra::commands {

// PING
CommandResult HandlePing(const astra::protocol::Command& command, CommandContext* context);

// INFO
CommandResult HandleInfo(const astra::protocol::Command& command, CommandContext* context);

// Admin commands are auto-registered via ASTRADB_REGISTER_COMMAND macro

}  // namespace astra::commands