// ==============================================================================
// Set Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "set_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/protocol/resp/resp_builder.hpp"

namespace astra::commands {

// SADD key member [member ...]
CommandResult HandleSAdd(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'SADD' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();
  size_t count = 0;

  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of member argument");
    }
    if (db->SAdd(key, arg.AsString())) {
      ++count;
    }
  }

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// SREM key member [member ...]
CommandResult HandleSRem(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'SREM' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();
  size_t count = 0;

  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of member argument");
    }
    if (db->SRem(key, arg.AsString())) {
      ++count;
    }
  }

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// SMEMBERS key
CommandResult HandleSMembers(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'SMEMBERS' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();
  auto members = db->SMembers(key);

  absl::InlinedVector<RespValue, 16> array;
  array.reserve(members.size());
  for (const auto& member : members) {
    array.emplace_back(RespValue(std::string(member)));
  }

  return CommandResult(RespValue(std::vector<RespValue>(array.begin(), array.end())));
}

// SISMEMBER key member
CommandResult HandleSIsMember(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'SISMEMBER' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& member_arg = command[1];

  if (!key_arg.IsBulkString() || !member_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string member = member_arg.AsString();

  bool is_member = db->SIsMember(key, member);
  return CommandResult(RespValue(static_cast<int64_t>(is_member ? 1 : 0)));
}

// SCARD key
CommandResult HandleSCard(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'SCARD' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();
  size_t count = db->SCard(key);
  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// SPOP key [count]
CommandResult HandleSPop(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'SPOP' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();
  
  // For now, only support popping one element
  auto member = db->SPop(key);
  if (!member.has_value()) {
    return CommandResult(RespValue());  // nil
  }
  
  // Log to AOF
  std::array<absl::string_view, 1> aof_args = {key};
  context->LogToAof("SPOP", aof_args);
  
  return CommandResult(RespValue(std::move(*member)));
}

// SRANDMEMBER key [count]
CommandResult HandleSRandMember(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'SRANDMEMBER' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();
  
  // For now, only support getting one random element
  auto member = db->SRandMember(key);
  if (!member.has_value()) {
    return CommandResult(RespValue());  // nil
  }
  
  return CommandResult(RespValue(std::move(*member)));
}

// SMOVE source destination member
CommandResult HandleSMove(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'SMOVE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& source_arg = command[0];
  const auto& dest_arg = command[1];
  const auto& member_arg = command[2];

  if (!source_arg.IsBulkString() || !dest_arg.IsBulkString() || !member_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of arguments for 'SMOVE' command");
  }

  std::string source = source_arg.AsString();
  std::string destination = dest_arg.AsString();
  std::string member = member_arg.AsString();

  bool moved = db->SMove(source, destination, member);
  
  return CommandResult(RespValue(static_cast<int64_t>(moved ? 1 : 0)));
}

// SINTER key [key ...]
CommandResult HandleSInter(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'SINTER' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  std::vector<std::string> keys;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  auto members = db->SInter(keys);
  
  RespValue result;
  std::vector<RespValue> resp_members;
  resp_members.reserve(members.size());
  for (const auto& member : members) {
    resp_members.push_back(RespValue(member));
  }
  result.SetArray(std::move(resp_members));
  return CommandResult(result);
}

// SUNION key [key ...]
CommandResult HandleSUnion(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'SUNION' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  std::vector<std::string> keys;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  auto members = db->SUnion(keys);
  
  RespValue result;
  std::vector<RespValue> resp_members;
  resp_members.reserve(members.size());
  for (const auto& member : members) {
    resp_members.push_back(RespValue(member));
  }
  result.SetArray(std::move(resp_members));
  return CommandResult(result);
}

// SDIFF key [key ...]
CommandResult HandleSDiff(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'SDIFF' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  std::vector<std::string> keys;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  auto members = db->SDiff(keys);
  
  RespValue result;
  std::vector<RespValue> resp_members;
  resp_members.reserve(members.size());
  for (const auto& member : members) {
    resp_members.push_back(RespValue(member));
  }
  result.SetArray(std::move(resp_members));
  return CommandResult(result);
}

// SINTERSTORE destination key [key ...]
CommandResult HandleSInterStore(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'SINTERSTORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& dest_arg = command[0];
  if (!dest_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of destination argument");
  }

  std::string destination = dest_arg.AsString();
  
  std::vector<std::string> keys;
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  size_t count = db->SInterStore(destination, keys);
  
  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// SUNIONSTORE destination key [key ...]
CommandResult HandleSUnionStore(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'SUNIONSTORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& dest_arg = command[0];
  if (!dest_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of destination argument");
  }

  std::string destination = dest_arg.AsString();
  
  std::vector<std::string> keys;
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  size_t count = db->SUnionStore(destination, keys);
  
  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// SDIFFSTORE destination key [key ...]
CommandResult HandleSDiffStore(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'SDIFFSTORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& dest_arg = command[0];
  if (!dest_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of destination argument");
  }

  std::string destination = dest_arg.AsString();
  
  std::vector<std::string> keys;
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  size_t count = db->SDiffStore(destination, keys);
  
  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// Auto-register all set commands
ASTRADB_REGISTER_COMMAND(SADD, -3, "write", RoutingStrategy::kByFirstKey, HandleSAdd);
ASTRADB_REGISTER_COMMAND(SREM, -3, "write", RoutingStrategy::kByFirstKey, HandleSRem);
ASTRADB_REGISTER_COMMAND(SMEMBERS, 2, "readonly", RoutingStrategy::kByFirstKey, HandleSMembers);
ASTRADB_REGISTER_COMMAND(SISMEMBER, 3, "readonly", RoutingStrategy::kByFirstKey, HandleSIsMember);
ASTRADB_REGISTER_COMMAND(SCARD, 2, "readonly", RoutingStrategy::kByFirstKey, HandleSCard);
ASTRADB_REGISTER_COMMAND(SMOVE, 3, "write", RoutingStrategy::kByFirstKey, HandleSMove);
ASTRADB_REGISTER_COMMAND(SINTER, -2, "readonly", RoutingStrategy::kNone, HandleSInter);
ASTRADB_REGISTER_COMMAND(SUNION, -2, "readonly", RoutingStrategy::kNone, HandleSUnion);
ASTRADB_REGISTER_COMMAND(SDIFF, -2, "readonly", RoutingStrategy::kNone, HandleSDiff);
ASTRADB_REGISTER_COMMAND(SINTERSTORE, -3, "write", RoutingStrategy::kByFirstKey, HandleSInterStore);
ASTRADB_REGISTER_COMMAND(SUNIONSTORE, -3, "write", RoutingStrategy::kByFirstKey, HandleSUnionStore);
ASTRADB_REGISTER_COMMAND(SDIFFSTORE, -3, "write", RoutingStrategy::kByFirstKey, HandleSDiffStore);
ASTRADB_REGISTER_COMMAND(SPOP, 2, "write", RoutingStrategy::kByFirstKey, HandleSPop);
ASTRADB_REGISTER_COMMAND(SRANDMEMBER, 2, "readonly", RoutingStrategy::kByFirstKey, HandleSRandMember);

}  // namespace astra::commands