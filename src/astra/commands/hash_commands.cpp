// ==============================================================================
// Hash Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "hash_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/protocol/resp/resp_builder.hpp"

namespace astra::commands {

// HSET key field value [field value ...]
CommandResult HandleHSet(const astra::protocol::Command& command, CommandContext* context) {
  // Need at least: key field value (3 args), plus pairs of field-value
  if (command.ArgCount() < 3 || (command.ArgCount() - 1) % 2 != 0) {
    return CommandResult(false, "ERR wrong number of arguments for 'HSET' command");
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
  size_t added = 0;

  // Process all field-value pairs
  for (size_t i = 1; i < command.ArgCount(); i += 2) {
    const auto& field_arg = command[i];
    const auto& value_arg = command[i + 1];

    if (!field_arg.IsBulkString() || !value_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of argument");
    }

    std::string field = field_arg.AsString();
    std::string value = value_arg.AsString();

    if (db->HSet(key, field, value)) {
      ++added;
    }
  }

  return CommandResult(RespValue(static_cast<int64_t>(added)));
}

// HGET key field
CommandResult HandleHGet(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'HGET' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& field_arg = command[1];

  if (!key_arg.IsBulkString() || !field_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string field = field_arg.AsString();

  auto value = db->HGet(key, field);
  if (value.has_value()) {
    return CommandResult(RespValue(std::string(*value)));
  } else {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }
}

// HDEL key field [field ...]
CommandResult HandleHDel(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'HDEL' command");
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
  std::vector<std::string> fields;
  fields.reserve(command.ArgCount() - 1);

  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of field argument");
    }
    fields.push_back(arg.AsString());
  }

  size_t count = db->HDel(key, fields);
  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// HEXISTS key field
CommandResult HandleHExists(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'HEXISTS' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& field_arg = command[1];

  if (!key_arg.IsBulkString() || !field_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string field = field_arg.AsString();

  bool exists = db->HExists(key, field);
  return CommandResult(RespValue(static_cast<int64_t>(exists ? 1 : 0)));
}

// HGETALL key
CommandResult HandleHGetAll(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'HGETALL' command");
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
  auto hash = db->HGetAll(key);

  std::vector<RespValue> array;
  array.reserve(hash.size() * 2);
  for (const auto& [field, value] : hash) {
    array.emplace_back(RespValue(std::string(field)));
    array.emplace_back(RespValue(std::string(value)));
  }

  return CommandResult(RespValue(std::move(array)));
}

// HLEN key
CommandResult HandleHLen(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'HLEN' command");
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
  size_t len = db->HLen(key);
  return CommandResult(RespValue(static_cast<int64_t>(len)));
}

// HKEYS key - Get all field names in a hash
CommandResult HandleHKeys(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'HKEYS' command");
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
  auto hash = db->HGetAll(key);

  std::vector<RespValue> array;
  array.reserve(hash.size());
  for (const auto& [field, _] : hash) {
    array.emplace_back(RespValue(std::string(field)));
  }

  return CommandResult(RespValue(std::move(array)));
}

// HVALS key - Get all values in a hash
CommandResult HandleHVals(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'HVALS' command");
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
  auto hash = db->HGetAll(key);

  std::vector<RespValue> array;
  array.reserve(hash.size());
  for (const auto& [_, value] : hash) {
    array.emplace_back(RespValue(std::string(value)));
  }

  return CommandResult(RespValue(std::move(array)));
}

// Auto-register all hash commands
ASTRADB_REGISTER_COMMAND(HSET, -4, "write", RoutingStrategy::kByFirstKey, HandleHSet);
ASTRADB_REGISTER_COMMAND(HGET, 3, "readonly", RoutingStrategy::kByFirstKey, HandleHGet);
ASTRADB_REGISTER_COMMAND(HDEL, -3, "write", RoutingStrategy::kByFirstKey, HandleHDel);
ASTRADB_REGISTER_COMMAND(HEXISTS, 3, "readonly", RoutingStrategy::kByFirstKey, HandleHExists);
ASTRADB_REGISTER_COMMAND(HGETALL, 2, "readonly", RoutingStrategy::kByFirstKey, HandleHGetAll);
ASTRADB_REGISTER_COMMAND(HLEN, 2, "readonly", RoutingStrategy::kByFirstKey, HandleHLen);
ASTRADB_REGISTER_COMMAND(HKEYS, 2, "readonly", RoutingStrategy::kByFirstKey, HandleHKeys);
ASTRADB_REGISTER_COMMAND(HVALS, 2, "readonly", RoutingStrategy::kByFirstKey, HandleHVals);

}  // namespace astra::commands