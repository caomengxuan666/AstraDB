// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "admin_commands.hpp"
#include "command_auto_register.hpp"

namespace astra::commands {

// PING
CommandResult HandlePing(const astra::protocol::Command& command, CommandContext* context) {
  RespValue pong;
  pong.SetString("PONG", protocol::RespType::kSimpleString);
  return CommandResult(pong);
}

// INFO
CommandResult HandleInfo(const astra::protocol::Command& command, CommandContext* context) {
  std::vector<RespValue> result;
  
  RespValue line1, line2, line3, line4, line5, line6, line7, line8, line9, line10;
  line1.SetString("# Server", protocol::RespType::kSimpleString);
  line2.SetString("version=1.0.0");
  line3.SetString("os=Linux");
  line4.SetString("arch=x86_64");
  line5.SetString("");  // Empty line
  line6.SetString("# Clients", protocol::RespType::kSimpleString);
  line7.SetString("connected_clients=1");
  line8.SetString("");  // Empty line
  line9.SetString("# Memory", protocol::RespType::kSimpleString);
  line10.SetString("used_memory_human=unknown");
  
  result.push_back(line1);
  result.push_back(line2);
  result.push_back(line3);
  result.push_back(line4);
  result.push_back(line5);
  result.push_back(line6);
  result.push_back(line7);
  result.push_back(line8);
  result.push_back(line9);
  result.push_back(line10);
  
  return CommandResult(RespValue(std::move(result)));
}

// Auto-register all admin commands
ASTRADB_REGISTER_COMMAND(PING, 0, "fast", RoutingStrategy::kNone, HandlePing);
ASTRADB_REGISTER_COMMAND(INFO, 0, "readonly", RoutingStrategy::kNone, HandleInfo);

}  // namespace astra::commands