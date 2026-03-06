// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "transaction_commands.hpp"

#include "astra/base/logging.hpp"
#include "command_auto_register.hpp"

namespace astra::commands {

CommandResult HandleMulti(const protocol::Command& command,
                          CommandContext* context) {
  (void)command;  // MULTI has no arguments

  if (context->IsInTransaction()) {
    return CommandResult(false, "ERR MULTI calls can not be nested");
  }

  context->BeginTransaction();

  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

CommandResult HandleExec(const protocol::Command& command,
                         CommandContext* context) {
  (void)command;  // EXEC has no arguments

  // Note: The actual EXEC execution is handled in server.cpp before this
  // handler. This is because we need access to the registry to execute queued
  // commands. This handler should never be reached in normal flow.

  if (!context->IsInTransaction()) {
    return CommandResult(false, "ERR EXEC without MULTI");
  }

  // If we reach here, just return the queued commands as an array
  // (fallback for non-server execution paths)
  auto queued = context->GetQueuedCommands();
  context->ClearQueuedCommands();
  context->ClearWatchedKeys();
  context->DiscardTransaction();

  RespValue response;
  response.SetArray({});
  return CommandResult(response);
}

CommandResult HandleDiscard(const protocol::Command& command,
                            CommandContext* context) {
  (void)command;  // DISCARD has no arguments

  if (!context->IsInTransaction()) {
    return CommandResult(false, "ERR DISCARD without MULTI");
  }

  context->DiscardTransaction();

  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

CommandResult HandleWatch(const protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'watch' command");
  }

  if (context->IsInTransaction()) {
    return CommandResult(false, "ERR WATCH inside MULTI is not allowed");
  }

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  // Watch all specified keys
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (arg.IsBulkString()) {
      const std::string& key = arg.AsString();
      uint64_t version = db->GetKeyVersion(key);
      context->WatchKey(key, version);
    }
  }

  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

CommandResult HandleUnwatch(const protocol::Command& command,
                            CommandContext* context) {
  (void)command;  // UNWATCH has no arguments

  context->ClearWatchedKeys();

  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// Register transaction commands
ASTRADB_REGISTER_COMMAND(MULTI, 1, "fast", RoutingStrategy::kNone, HandleMulti);
ASTRADB_REGISTER_COMMAND(EXEC, 1, "exclusive", RoutingStrategy::kNone,
                         HandleExec);
ASTRADB_REGISTER_COMMAND(DISCARD, 1, "fast", RoutingStrategy::kNone,
                         HandleDiscard);
ASTRADB_REGISTER_COMMAND(WATCH, -2, "fast", RoutingStrategy::kNone,
                         HandleWatch);
ASTRADB_REGISTER_COMMAND(UNWATCH, 1, "fast", RoutingStrategy::kNone,
                         HandleUnwatch);

}  // namespace astra::commands