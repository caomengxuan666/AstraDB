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

// HINCRBYFLOAT key field increment
CommandResult HandleHIncrByFloat(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'HINCRBYFLOAT' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& field_arg = command[1];
  const auto& incr_arg = command[2];

  if (!key_arg.IsBulkString() || !field_arg.IsBulkString() || !incr_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string field = field_arg.AsString();
  double increment;
  
  try {
    if (!absl::SimpleAtod(incr_arg.AsString(), &increment)) {
      return CommandResult(false, "ERR value is not a valid float");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not a valid float");
  }

  double new_value = db->HIncrByFloat(key, field, increment);
  
  // Log to AOF
  std::string incr_str = incr_arg.AsString();
  std::array<absl::string_view, 3> aof_args = {key, field, incr_str};
  context->LogToAof("HINCRBYFLOAT", aof_args);
  
  return CommandResult(RespValue(new_value));
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

  absl::InlinedVector<RespValue, 16> array;
  array.reserve(hash.size() * 2);
  for (const auto& [field, value] : hash) {
    array.emplace_back(RespValue(std::string(field)));
    array.emplace_back(RespValue(std::string(value)));
  }

  return CommandResult(RespValue(std::vector<RespValue>(array.begin(), array.end())));
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

  absl::InlinedVector<RespValue, 16> array;
  array.reserve(hash.size());
  for (const auto& [field, _] : hash) {
    array.emplace_back(RespValue(std::string(field)));
  }

  return CommandResult(RespValue(std::vector<RespValue>(array.begin(), array.end())));
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

  absl::InlinedVector<RespValue, 16> array;
  array.reserve(hash.size());
  for (const auto& [_, value] : hash) {
    array.emplace_back(RespValue(std::string(value)));
  }

  return CommandResult(RespValue(std::vector<RespValue>(array.begin(), array.end())));
}

// HINCRBY key field increment
CommandResult HandleHIncrBy(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'HINCRBY' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& field_arg = command[1];
  const auto& incr_arg = command[2];

  if (!key_arg.IsBulkString() || !field_arg.IsBulkString() || !incr_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string field = field_arg.AsString();
  int64_t increment;

  try {
    if (!absl::SimpleAtoi(incr_arg.AsString(), &increment)) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  int64_t result = db->HIncrBy(key, field, increment);
  return CommandResult(RespValue(result));
}

// HSETNX key field value
CommandResult HandleHSetNx(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'HSETNX' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& field_arg = command[1];
  const auto& value_arg = command[2];

  if (!key_arg.IsBulkString() || !field_arg.IsBulkString() || !value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string field = field_arg.AsString();
  std::string value = value_arg.AsString();

  bool result = db->HSetNx(key, field, value);
  return CommandResult(RespValue(static_cast<int64_t>(result ? 1 : 0)));
}

// HMGET key field [field ...]
CommandResult HandleHMGet(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'HMGET' command");
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
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    if (!command[i].IsBulkString()) {
      return CommandResult(false, "ERR wrong type of field argument");
    }
    fields.push_back(command[i].AsString());
  }

  auto results = db->HMGet(key, fields);
  absl::InlinedVector<RespValue, 16> array;
  array.reserve(results.size());
  for (const auto& val : results) {
    if (val.has_value()) {
      array.emplace_back(RespValue(val.value()));
    } else {
      // Default constructed RespValue is null
      array.emplace_back(RespValue());
    }
  }

  return CommandResult(RespValue(std::vector<RespValue>(array.begin(), array.end())));
}

// HSTRLEN key field - Get the length of the value associated with field in the hash
CommandResult HandleHStrLen(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'HSTRLEN' command");
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
  if (!value.has_value()) {
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  }

  return CommandResult(RespValue(static_cast<int64_t>(value.value().length())));
}

// HRANDFIELD key [count [WITHVALUES]] - Get one or more random fields from a hash
CommandResult HandleHRandField(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1 || command.ArgCount() > 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'HRANDFIELD' command");
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
  auto all_data = db->HGetAll(key);

  if (all_data.empty()) {
    return CommandResult(RespValue(RespType::kNullArray));
  }

  // Extract field names
  std::vector<std::string> all_fields;
  for (const auto& [field, value] : all_data) {
    all_fields.push_back(field);
  }

  // Parse arguments
  int64_t count = 1;
  bool with_values = false;

  if (command.ArgCount() >= 2) {
    const auto& count_arg = command[1];
    if (!count_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of count argument");
    }
    if (!absl::SimpleAtoi(count_arg.AsString(), &count)) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  }

  if (command.ArgCount() >= 3) {
    const auto& opt_arg = command[2];
    if (!opt_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of option argument");
    }
    if (opt_arg.AsString() == "WITHVALUES") {
      with_values = true;
    }
  }

  // Get random fields
  std::vector<std::string> random_fields;
  if (count > 0) {
    // Get unique random fields (when count is positive)
    size_t actual_count = static_cast<size_t>(std::min(count, static_cast<int64_t>(all_fields.size())));
    std::vector<size_t> indices(all_fields.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), std::mt19937(std::random_device()()));
    
    for (size_t i = 0; i < actual_count; ++i) {
      random_fields.push_back(all_fields[indices[i]]);
    }
  } else {
    // Get fields with duplicates (when count is negative)
    size_t actual_count = static_cast<size_t>(-count);
    static absl::BitGen bitgen;
    for (size_t i = 0; i < actual_count; ++i) {
      size_t idx = absl::Uniform<size_t>(bitgen, 0, all_fields.size());
      random_fields.push_back(all_fields[idx]);
    }
  }

  // Build response
  std::vector<RespValue> result;
  if (with_values) {
    for (const auto& field : random_fields) {
      result.emplace_back(RespValue(field));
      auto value = db->HGet(key, field);
      if (value.has_value()) {
        result.emplace_back(RespValue(value.value()));
      } else {
        result.emplace_back(RespValue(RespType::kNullBulkString));
      }
    }
  } else {
    for (const auto& field : random_fields) {
      result.emplace_back(RespValue(field));
    }
  }

  return CommandResult(RespValue(std::move(result)));
}

// HMSET key field value [field value ...] - Set multiple hash fields (deprecated in Redis 4+, but we support it)
CommandResult HandleHMSet(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 3 || command.ArgCount() % 2 == 0) {
    return CommandResult(false, "ERR wrong number of arguments for 'HMSET' command");
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

  for (size_t i = 1; i < command.ArgCount(); i += 2) {
    const auto& field_arg = command[i];
    const auto& value_arg = command[i + 1];

    if (!field_arg.IsBulkString() || !value_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of field or value argument");
    }

    db->HSet(key, field_arg.AsString(), value_arg.AsString());
  }

  RespValue response;
  response.SetString("OK", RespType::kSimpleString);
  return CommandResult(response);
}

// HTTL key field [field ...] - Get TTL of hash fields
CommandResult HandleHTTL(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'HTTL' command");
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

  // Check if key exists and is a hash
  auto key_type = db->GetType(key);
  if (!key_type.has_value() || key_type.value() != astra::storage::KeyType::kHash) {
    std::vector<RespValue> result(command.ArgCount() - 1, RespValue(static_cast<int64_t>(-2)));
    return CommandResult(RespValue(std::move(result)));
  }

  std::vector<RespValue> result;
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& field_arg = command[i];
    if (!field_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of field argument");
    }

    // For now, return -1 (no expiration) for all fields
    // Full implementation would require per-field expiration support
    result.emplace_back(RespValue(static_cast<int64_t>(-1)));
  }

  return CommandResult(RespValue(std::move(result)));
}

// HSCAN key cursor [MATCH pattern] [COUNT count] - Incrementally iterate hash fields
CommandResult HandleHScan(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'HSCAN' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& cursor_arg = command[1];

  if (!key_arg.IsBulkString() || !cursor_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string cursor_str = cursor_arg.AsString();

  uint64_t cursor;
  if (!absl::SimpleAtoi(cursor_str, &cursor)) {
    return CommandResult(false, "ERR invalid cursor");
  }

  // Get all fields from hash
  auto all_data = db->HGetAll(key);
  
  // Extract field names
  std::vector<std::string> all_fields;
  for (const auto& [field, value] : all_data) {
    all_fields.push_back(field);
  }
  
  // Parse options
  std::string pattern = "*";
  size_t count = 10;

  for (size_t i = 2; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of option argument");
    }

    std::string opt = arg.AsString();
    if (opt == "MATCH" && i + 1 < command.ArgCount()) {
      pattern = command[++i].AsString();
    } else if (opt == "COUNT" && i + 1 < command.ArgCount()) {
      if (!absl::SimpleAtoi(command[++i].AsString(), &count)) {
        return CommandResult(false, "ERR value is not an integer or out of range");
      }
    }
  }

  // Filter fields by pattern
  std::vector<std::string> matched_fields;
  for (const auto& field : all_fields) {
    bool matches = false;
    if (pattern == "*") {
      matches = true;
    } else if (pattern[0] == '*' && pattern.back() == '*' && pattern.size() > 1) {
      // *middle* - contains
      std::string middle = pattern.substr(1, pattern.size() - 2);
      matches = (field.find(middle) != std::string::npos);
    } else if (pattern[0] == '*' && pattern.size() > 1) {
      // *suffix - ends with
      std::string suffix = pattern.substr(1);
      matches = (field.size() >= suffix.size() && field.substr(field.size() - suffix.size()) == suffix);
    } else if (pattern.back() == '*' && pattern.size() > 1) {
      // prefix* - starts with
      std::string prefix = pattern.substr(0, pattern.size() - 1);
      matches = (field.size() >= prefix.size() && field.substr(0, prefix.size()) == prefix);
    } else {
      // exact match
      matches = (field == pattern);
    }

    if (matches) {
      matched_fields.push_back(field);
    }
  }

  // Get current page
  std::vector<RespValue> result_array;
  size_t start = static_cast<size_t>(cursor);
  size_t end = std::min(start + count, matched_fields.size());
  
  for (size_t i = start; i < end; ++i) {
    const auto& field = matched_fields[i];
    result_array.emplace_back(RespValue(field));
    
    auto value = db->HGet(key, field);
    if (value.has_value()) {
      result_array.emplace_back(RespValue(value.value()));
    } else {
      result_array.emplace_back(RespValue(RespType::kNullBulkString));
    }
  }

  // Build response
  std::vector<RespValue> response;
  
  // New cursor
  uint64_t new_cursor = (end >= matched_fields.size()) ? 0 : end;
  RespValue cursor_val;
  cursor_val.SetString(std::to_string(new_cursor), RespType::kBulkString);
  response.emplace_back(cursor_val);
  
  // Results
  response.emplace_back(RespValue(std::move(result_array)));

  return CommandResult(RespValue(std::move(response)));
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
ASTRADB_REGISTER_COMMAND(HINCRBY, 4, "write", RoutingStrategy::kByFirstKey, HandleHIncrBy);
ASTRADB_REGISTER_COMMAND(HINCRBYFLOAT, 4, "write", RoutingStrategy::kByFirstKey, HandleHIncrByFloat);
ASTRADB_REGISTER_COMMAND(HSETNX, 4, "write", RoutingStrategy::kByFirstKey, HandleHSetNx);
ASTRADB_REGISTER_COMMAND(HMGET, -3, "readonly", RoutingStrategy::kByFirstKey, HandleHMGet);
ASTRADB_REGISTER_COMMAND(HSTRLEN, 3, "readonly", RoutingStrategy::kByFirstKey, HandleHStrLen);
ASTRADB_REGISTER_COMMAND(HRANDFIELD, -2, "readonly", RoutingStrategy::kByFirstKey, HandleHRandField);
ASTRADB_REGISTER_COMMAND(HMSET, -4, "write", RoutingStrategy::kByFirstKey, HandleHMSet);
ASTRADB_REGISTER_COMMAND(HTTL, -2, "readonly", RoutingStrategy::kByFirstKey, HandleHTTL);
ASTRADB_REGISTER_COMMAND(HSCAN, -3, "readonly", RoutingStrategy::kByFirstKey, HandleHScan);

}  // namespace astra::commands