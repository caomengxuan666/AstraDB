// ==============================================================================
// List Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "list_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/protocol/resp/resp_builder.hpp"
#include <algorithm>
#include <sstream>

namespace astra::commands {

// LPUSH key value [value ...]
CommandResult HandleLPush(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'LPUSH' command");
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
  
  // Push all values (in reverse order since LPUSH adds to head)
  for (size_t i = command.ArgCount() - 1; i >= 1; --i) {
    const auto& value_arg = command[i];
    if (!value_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of value argument");
    }
    db->LPush(key, value_arg.AsString());
  }

  return CommandResult(RespValue(static_cast<int64_t>(command.ArgCount() - 1)));
}

// RPUSH key value [value ...]
CommandResult HandleRPush(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'RPUSH' command");
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
  
  // Push all values
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& value_arg = command[i];
    if (!value_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of value argument");
    }
    db->RPush(key, value_arg.AsString());
  }

  return CommandResult(RespValue(static_cast<int64_t>(command.ArgCount() - 1)));
}

// LPOP key
CommandResult HandleLPop(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'LPOP' command");
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
  auto value = db->LPop(key);

  if (!value.has_value()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  return CommandResult(RespValue(*value));
}

// RPOP key
CommandResult HandleRPop(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'RPOP' command");
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
  auto value = db->RPop(key);

  if (!value.has_value()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  return CommandResult(RespValue(*value));
}

// LLEN key
CommandResult HandleLLen(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'LLEN' command");
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
  size_t len = db->LLen(key);

  return CommandResult(RespValue(static_cast<int64_t>(len)));
}

// LINDEX key index
CommandResult HandleLIndex(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'LINDEX' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& index_arg = command[1];

  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  if (!index_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of index argument");
  }

  std::string key = key_arg.AsString();
  std::string index_str = index_arg.AsString();

  char* endptr = nullptr;
  int64_t index = std::strtoll(index_str.c_str(), &endptr, 10);
  if (endptr == index_str.c_str() || *endptr != '\0') {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  auto value = db->LIndex(key, index);

  if (!value.has_value()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  return CommandResult(RespValue(*value));
}

// LSET key index value
CommandResult HandleLSet(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'LSET' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& index_arg = command[1];
  const auto& value_arg = command[2];

  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  if (!index_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of index argument");
  }

  if (!value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of value argument");
  }

  std::string key = key_arg.AsString();
  std::string index_str = index_arg.AsString();
  std::string value = value_arg.AsString();

  char* endptr = nullptr;
  int64_t index = std::strtoll(index_str.c_str(), &endptr, 10);
  if (endptr == index_str.c_str() || *endptr != '\0') {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  bool success = db->LSet(key, index, value);

  if (!success) {
    return CommandResult(false, "ERR no such key or index out of range");
  }

  RespValue result(RespType::kSimpleString);
  result.SetString("OK", RespType::kSimpleString);
  return CommandResult(result);
}

// LRANGE key start stop
CommandResult HandleLRange(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'LRANGE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& start_arg = command[1];
  const auto& stop_arg = command[2];

  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  if (!start_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of start argument");
  }

  if (!stop_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of stop argument");
  }

  std::string key = key_arg.AsString();
  std::string start_str = start_arg.AsString();
  std::string stop_str = stop_arg.AsString();

  char* endptr = nullptr;
  int64_t start = std::strtoll(start_str.c_str(), &endptr, 10);
  if (endptr == start_str.c_str() || *endptr != '\0') {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  endptr = nullptr;
  int64_t stop = std::strtoll(stop_str.c_str(), &endptr, 10);
  if (endptr == stop_str.c_str() || *endptr != '\0') {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  auto values = db->LRange(key, start, stop);

  std::vector<RespValue> array;
  array.reserve(values.size());
  for (const auto& val : values) {
    array.push_back(RespValue(val));
  }

  return CommandResult(RespValue(std::move(array)));
}

// LTRIM key start stop
CommandResult HandleLTrim(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'LTRIM' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& start_arg = command[1];
  const auto& stop_arg = command[2];

  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  if (!start_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of start argument");
  }

  if (!stop_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of stop argument");
  }

  std::string key = key_arg.AsString();
  std::string start_str = start_arg.AsString();
  std::string stop_str = stop_arg.AsString();

  char* endptr = nullptr;
  int64_t start = std::strtoll(start_str.c_str(), &endptr, 10);
  if (endptr == start_str.c_str() || *endptr != '\0') {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  endptr = nullptr;
  int64_t stop = std::strtoll(stop_str.c_str(), &endptr, 10);
  if (endptr == stop_str.c_str() || *endptr != '\0') {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  bool success = db->LTrim(key, start, stop);

  if (!success) {
    return CommandResult(false, "ERR no such key");
  }

  RespValue result(RespType::kSimpleString);
  result.SetString("OK", RespType::kSimpleString);
  return CommandResult(result);
}

// LREM key count value
CommandResult HandleLRem(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'LREM' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& count_arg = command[1];
  const auto& value_arg = command[2];

  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  if (!count_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of count argument");
  }

  if (!value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of value argument");
  }

  std::string key = key_arg.AsString();
  std::string count_str = count_arg.AsString();
  std::string value = value_arg.AsString();

  char* endptr = nullptr;
  int64_t count = std::strtoll(count_str.c_str(), &endptr, 10);
  if (endptr == count_str.c_str() || *endptr != '\0') {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  size_t removed = db->LRem(key, value, count);

  return CommandResult(RespValue(static_cast<int64_t>(removed)));
}

// LINSERT key BEFORE|AFTER pivot value
CommandResult HandleLInsert(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 4) {
    return CommandResult(false, "ERR wrong number of arguments for 'LINSERT' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& where_arg = command[1];
  const auto& pivot_arg = command[2];
  const auto& value_arg = command[3];

  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  if (!where_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of where argument");
  }

  if (!pivot_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of pivot argument");
  }

  if (!value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of value argument");
  }

  std::string key = key_arg.AsString();
  std::string where = where_arg.AsString();
  std::string pivot = pivot_arg.AsString();
  std::string value = value_arg.AsString();

  // Check if where is valid
  if (where != "BEFORE" && where != "AFTER") {
    return CommandResult(false, "ERR syntax error");
  }

  // Find pivot index
  auto values = db->LRange(key, 0, -1);
  int64_t pivot_index = -1;
  for (size_t i = 0; i < values.size(); ++i) {
    if (values[i] == pivot) {
      pivot_index = static_cast<int64_t>(i);
      break;
    }
  }

  if (pivot_index == -1) {
    return CommandResult(RespValue(static_cast<int64_t>(-1)));
  }

  bool before = (where == "BEFORE");
  bool success = db->LInsert(key, pivot_index, value, before);

  if (!success) {
    return CommandResult(RespValue(static_cast<int64_t>(-1)));
  }

  // Return new length
  return CommandResult(RespValue(static_cast<int64_t>(db->LLen(key))));
}

// RPOPLPUSH source destination
CommandResult HandleRPopLPush(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'RPOPLPUSH' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& source_arg = command[0];
  const auto& dest_arg = command[1];

  if (!source_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of source argument");
  }

  if (!dest_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of destination argument");
  }

  std::string source = source_arg.AsString();
  std::string dest = dest_arg.AsString();

  auto value = db->RPopLPush(source, dest);

  if (!value.has_value()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  return CommandResult(RespValue(*value));
}

// Auto-register all list commands
ASTRADB_REGISTER_COMMAND(LPUSH, -2, "write", RoutingStrategy::kByFirstKey, HandleLPush);
ASTRADB_REGISTER_COMMAND(RPUSH, -2, "write", RoutingStrategy::kByFirstKey, HandleRPush);
ASTRADB_REGISTER_COMMAND(LPOP, 1, "write", RoutingStrategy::kByFirstKey, HandleLPop);
ASTRADB_REGISTER_COMMAND(RPOP, 1, "write", RoutingStrategy::kByFirstKey, HandleRPop);
ASTRADB_REGISTER_COMMAND(LLEN, 1, "readonly", RoutingStrategy::kByFirstKey, HandleLLen);
ASTRADB_REGISTER_COMMAND(LINDEX, 2, "readonly", RoutingStrategy::kByFirstKey, HandleLIndex);
ASTRADB_REGISTER_COMMAND(LSET, 3, "write", RoutingStrategy::kByFirstKey, HandleLSet);
ASTRADB_REGISTER_COMMAND(LRANGE, 3, "readonly", RoutingStrategy::kByFirstKey, HandleLRange);
ASTRADB_REGISTER_COMMAND(LTRIM, 3, "write", RoutingStrategy::kByFirstKey, HandleLTrim);
ASTRADB_REGISTER_COMMAND(LREM, 3, "write", RoutingStrategy::kByFirstKey, HandleLRem);
ASTRADB_REGISTER_COMMAND(LINSERT, 4, "write", RoutingStrategy::kByFirstKey, HandleLInsert);
ASTRADB_REGISTER_COMMAND(RPOPLPUSH, 2, "write", RoutingStrategy::kByFirstKey, HandleRPopLPush);

}  // namespace astra::commands