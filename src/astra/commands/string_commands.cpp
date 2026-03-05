// ==============================================================================
// String Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "string_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/protocol/resp/resp_builder.hpp"

namespace astra::commands {

// GET key
CommandResult HandleGet(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'GET' command");
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
  auto value = db->Get(key);

  if (value.has_value()) {
    return CommandResult(RespValue(std::string(value->value)));
  } else {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }
}

// SET key value [NX|XX] [EX seconds | PX milliseconds]
CommandResult HandleSet(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'SET' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& value_arg = command[1];

  if (!key_arg.IsBulkString() || !value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key or value argument");
  }

std::string key = key_arg.AsString();
  std::string value = value_arg.AsString();
  std::optional<int64_t> expire_time_ms;

  // Parse options
  bool nx = false;
  bool xx = false;

  for (size_t i = 2; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of option argument");
    }
    std::string opt = arg.AsString();

    if (opt == "NX") {
      nx = true;
    } else if (opt == "XX") {
      xx = true;
    } else if (opt == "EX") {
      // EX seconds
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR syntax error");
      }
      const auto& seconds_arg = command[i + 1];
      if (!seconds_arg.IsBulkString()) {
        return CommandResult(false, "ERR wrong type of seconds argument");
      }
      try {
        int64_t seconds;
        if (!absl::SimpleAtoi(seconds_arg.AsString(), &seconds)) {
          return CommandResult(false, "ERR value is not an integer or out of range");
        }
        if (seconds < 0) {
          return CommandResult(false, "ERR invalid expire time");
        }
        expire_time_ms = astra::storage::KeyMetadata::GetCurrentTimeMs() + (seconds * 1000);
      } catch (...) {
        return CommandResult(false, "ERR value is not an integer or out of range");
      }
      ++i;
    } else if (opt == "PX") {
      // PX milliseconds
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR syntax error");
      }
      const auto& millis_arg = command[i + 1];
      if (!millis_arg.IsBulkString()) {
        return CommandResult(false, "ERR wrong type of milliseconds argument");
      }
      try {
        int64_t millis;
        if (!absl::SimpleAtoi(millis_arg.AsString(), &millis)) {
          return CommandResult(false, "ERR value is not an integer or out of range");
        }
        if (millis < 0) {
          return CommandResult(false, "ERR invalid expire time");
        }
        expire_time_ms = astra::storage::KeyMetadata::GetCurrentTimeMs() + millis;
      } catch (...) {
        return CommandResult(false, "ERR value is not an integer or out of range");
      }
      ++i;
    } else {
      return CommandResult(false, "ERR syntax error");
    }
  }

  if (nx && xx) {
    return CommandResult(false, "ERR NX and XX options at the same time are not compatible");
  }

  // Check if key exists
  bool key_exists = db->Get(key).has_value();

  // Apply NX/XX logic
  if (nx && key_exists) {
    return CommandResult(RespValue(RespType::kNullBulkString));  // Already exists
  }
  if (xx && !key_exists) {
    return CommandResult(RespValue(RespType::kNullBulkString));  // Does not exist
  }

  // Set the key
  db->Set(key, value);
  
  // Set expiration if specified
  if (expire_time_ms.has_value()) {
    db->SetExpireMs(key, *expire_time_ms);
  }
  
  // Log to AOF (zero-copy with absl::Span)
  if (expire_time_ms.has_value()) {
    std::string px_str = absl::StrCat(*expire_time_ms);
    std::array<absl::string_view, 4> aof_args_with_expire = {key, value, "PX", px_str};
    context->LogToAof("SET", absl::MakeSpan(aof_args_with_expire));
  } else {
    std::array<absl::string_view, 2> aof_args_simple = {key, value};
    context->LogToAof("SET", absl::MakeSpan(aof_args_simple));
  }
  
  RespValue response;
  response.SetString("OK", RespType::kSimpleString);
  return CommandResult(response);
}

// DEL key [key ...]
CommandResult HandleDel(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() == 0) {
    return CommandResult(false, "ERR wrong number of arguments for 'DEL' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  std::vector<std::string> keys;
  keys.reserve(command.ArgCount());
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  size_t count = db->Del(keys);
  
  // Log to AOF (convert to string_view span)
  std::vector<absl::string_view> key_views;
  key_views.reserve(keys.size());
  for (const auto& k : keys) {
    key_views.emplace_back(k);
  }
  context->LogToAof("DEL", absl::MakeSpan(key_views));
  
  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// MGET key [key ...]
CommandResult HandleMGet(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() == 0) {
    return CommandResult(false, "ERR wrong number of arguments for 'MGET' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  std::vector<std::string> keys;
  keys.reserve(command.ArgCount());
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  auto results = db->MGet(keys);

  std::vector<RespValue> array;
  array.reserve(results.size());
  for (const auto& result : results) {
    if (result.has_value()) {
      array.emplace_back(RespValue(std::string(*result)));
    } else {
      array.emplace_back(RespValue(RespType::kNullBulkString));
    }
  }

  return CommandResult(RespValue(std::move(array)));
}

// MSET key value [key value ...]
CommandResult HandleMSet(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() == 0 || command.ArgCount() % 2 != 0) {
    return CommandResult(false, "ERR wrong number of arguments for 'MSET' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  for (size_t i = 0; i < command.ArgCount(); i += 2) {
    const auto& key_arg = command[i];
    const auto& value_arg = command[i + 1];

    if (!key_arg.IsBulkString() || !value_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key or value argument");
    }

    db->Set(key_arg.AsString(), StringValue(value_arg.AsString()));
  }

  // Log to AOF (zero-copy with string_view from const std::string&)
  std::vector<absl::string_view> aof_args;
  aof_args.reserve(command.ArgCount());
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    aof_args.emplace_back(command[i].AsString());
  }
  context->LogToAof("MSET", absl::MakeSpan(aof_args));

  RespValue response;
  response.SetString("OK", RespType::kSimpleString);
  return CommandResult(response);
}

// INCR key
CommandResult HandleIncr(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'INCR' command");
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
  auto value = db->Get(key);
  
  int64_t int_value = 0;
  if (value.has_value()) {
    // Try to parse as integer
    try {
      if (!absl::SimpleAtoi(value->value, &int_value)) {
        return CommandResult(false, "ERR value is not an integer or out of range");
      }
    } catch (...) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  }
  
  // Increment
  int_value++;
  
  // Store back
  db->Set(key, absl::StrCat(int_value));
  
  // Log to AOF
  std::array<absl::string_view, 1> aof_args = {key};
  context->LogToAof("INCR", aof_args);
  
  return CommandResult(RespValue(int_value));
}

// DECR key
CommandResult HandleDecr(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'DECR' command");
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
  auto value = db->Get(key);
  
  int64_t int_value = 0;
  if (value.has_value()) {
    try {
      if (!absl::SimpleAtoi(value->value, &int_value)) {
        return CommandResult(false, "ERR value is not an integer or out of range");
      }
    } catch (...) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  }
  
  int_value--;
  db->Set(key, absl::StrCat(int_value));
  
  std::array<absl::string_view, 1> aof_args = {key};
  context->LogToAof("DECR", aof_args);
  
  return CommandResult(RespValue(int_value));
}

// INCRBY key increment
CommandResult HandleIncrBy(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'INCRBY' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& incr_arg = command[1];
  
  if (!key_arg.IsBulkString() || !incr_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  int64_t increment;
  
  try {
    if (!absl::SimpleAtoi(incr_arg.AsString(), &increment)) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  auto value = db->Get(key);
  
  int64_t int_value = 0;
  if (value.has_value()) {
    try {
      if (!absl::SimpleAtoi(value->value, &int_value)) {
        return CommandResult(false, "ERR value is not an integer or out of range");
      }
    } catch (...) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  }
  
  int_value += increment;
  db->Set(key, absl::StrCat(int_value));
  
  std::array<absl::string_view, 2> aof_args = {key, incr_arg.AsString()};
  context->LogToAof("INCRBY", aof_args);
  
  return CommandResult(RespValue(int_value));
}

// DECRBY key decrement
CommandResult HandleDecrBy(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'DECRBY' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& decr_arg = command[1];
  
  if (!key_arg.IsBulkString() || !decr_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  int64_t decrement;
  
  try {
    if (!absl::SimpleAtoi(decr_arg.AsString(), &decrement)) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  auto value = db->Get(key);
  
  int64_t int_value = 0;
  if (value.has_value()) {
    try {
      if (!absl::SimpleAtoi(value->value, &int_value)) {
        return CommandResult(false, "ERR value is not an integer or out of range");
      }
    } catch (...) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  }
  
  int_value -= decrement;
  db->Set(key, absl::StrCat(int_value));
  
  std::array<absl::string_view, 2> aof_args = {key, decr_arg.AsString()};
  context->LogToAof("DECRBY", aof_args);
  
  return CommandResult(RespValue(int_value));
}

// APPEND key value
CommandResult HandleAppend(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'APPEND' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& value_arg = command[1];
  
  if (!key_arg.IsBulkString() || !value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string append_value = value_arg.AsString();
  
  auto existing = db->Get(key);
  std::string new_value;
  
  if (existing.has_value()) {
    new_value = existing->value + append_value;
  } else {
    new_value = append_value;
  }
  
  db->Set(key, new_value);
  
  std::array<absl::string_view, 2> aof_args = {key, append_value};
  context->LogToAof("APPEND", aof_args);
  
  return CommandResult(RespValue(static_cast<int64_t>(new_value.size())));
}

// STRLEN key
CommandResult HandleStrlen(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'STRLEN' command");
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
  auto value = db->Get(key);
  
  if (value.has_value()) {
    return CommandResult(RespValue(static_cast<int64_t>(value->value.size())));
  } else {
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  }
}

// GETSET key value
CommandResult HandleGetSet(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'GETSET' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& value_arg = command[1];
  
  if (!key_arg.IsBulkString() || !value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string new_value = value_arg.AsString();
  
  auto old_value = db->Get(key);
  db->Set(key, new_value);
  
  // Log to AOF
  std::array<absl::string_view, 2> aof_args = {key, new_value};
  context->LogToAof("GETSET", aof_args);
  
  if (old_value.has_value()) {
    return CommandResult(RespValue(std::string(old_value->value)));
  } else {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }
}

// SETEX key seconds value
CommandResult HandleSetEx(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'SETEX' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& seconds_arg = command[1];
  const auto& value_arg = command[2];
  
  if (!key_arg.IsBulkString() || !seconds_arg.IsBulkString() || !value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string value = value_arg.AsString();
  
  int64_t seconds;
  try {
    if (!absl::SimpleAtoi(seconds_arg.AsString(), &seconds)) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
    if (seconds < 0) {
      return CommandResult(false, "ERR invalid expire time");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }
  
  db->Set(key, value);
  db->SetExpireSeconds(key, seconds);
  
  // Log to AOF
  std::array<absl::string_view, 3> aof_args = {key, seconds_arg.AsString(), value};
  context->LogToAof("SETEX", aof_args);
  
  RespValue response;
  response.SetString("OK", RespType::kSimpleString);
  return CommandResult(response);
}

// SETNX key value
CommandResult HandleSetNx(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'SETNX' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& value_arg = command[1];
  
  if (!key_arg.IsBulkString() || !value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string value = value_arg.AsString();
  
  // Check if key already exists
  if (db->Get(key).has_value()) {
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  }
  
  db->Set(key, value);
  
  // Log to AOF
  std::array<absl::string_view, 2> aof_args = {key, value};
  context->LogToAof("SETNX", aof_args);
  
  return CommandResult(RespValue(static_cast<int64_t>(1)));
}

// GETRANGE key start end
CommandResult HandleGetRange(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'GETRANGE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& start_arg = command[1];
  const auto& end_arg = command[2];

  if (!key_arg.IsBulkString() || !start_arg.IsBulkString() || !end_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  int64_t start, end;
  
  try {
    if (!absl::SimpleAtoi(start_arg.AsString(), &start)) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
    if (!absl::SimpleAtoi(end_arg.AsString(), &end)) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  std::string result = db->GetRange(key, start, end);
  return CommandResult(RespValue(std::move(result)));
}

// SETRANGE key offset value
CommandResult HandleSetRange(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'SETRANGE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& offset_arg = command[1];
  const auto& value_arg = command[2];

  if (!key_arg.IsBulkString() || !offset_arg.IsBulkString() || !value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string value = value_arg.AsString();
  int64_t offset;
  
  try {
    if (!absl::SimpleAtoi(offset_arg.AsString(), &offset)) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
    if (offset < 0) {
      return CommandResult(false, "ERR offset is out of range");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  size_t new_len = db->SetRange(key, offset, value);
  
  // Log to AOF
  std::array<absl::string_view, 3> aof_args = {key, offset_arg.AsString(), value};
  context->LogToAof("SETRANGE", aof_args);
  
  return CommandResult(RespValue(static_cast<int64_t>(new_len)));
}

// EXISTS key [key ...]
CommandResult HandleExists(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() == 0) {
    return CommandResult(false, "ERR wrong number of arguments for 'EXISTS' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  size_t count = 0;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    // Create a copy of the key string (same pattern as TYPE command)
    std::string key = arg.AsString();
    if (db->GetType(key).has_value()) {
      ++count;
    }
  }

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// Auto-register all string commands
ASTRADB_REGISTER_COMMAND(GET, 2, "readonly", RoutingStrategy::kByFirstKey, HandleGet);
ASTRADB_REGISTER_COMMAND(SET, -3, "write", RoutingStrategy::kByFirstKey, HandleSet);
ASTRADB_REGISTER_COMMAND(DEL, -2, "write", RoutingStrategy::kByFirstKey, HandleDel);
ASTRADB_REGISTER_COMMAND(MGET, -2, "readonly", RoutingStrategy::kNone, HandleMGet);
ASTRADB_REGISTER_COMMAND(MSET, -3, "write", RoutingStrategy::kNone, HandleMSet);
ASTRADB_REGISTER_COMMAND(EXISTS, -2, "readonly", RoutingStrategy::kByFirstKey, HandleExists);

// Increment/Decrement commands
ASTRADB_REGISTER_COMMAND(INCR, 2, "write", RoutingStrategy::kByFirstKey, HandleIncr);
ASTRADB_REGISTER_COMMAND(DECR, 2, "write", RoutingStrategy::kByFirstKey, HandleDecr);
ASTRADB_REGISTER_COMMAND(INCRBY, 3, "write", RoutingStrategy::kByFirstKey, HandleIncrBy);
ASTRADB_REGISTER_COMMAND(DECRBY, 3, "write", RoutingStrategy::kByFirstKey, HandleDecrBy);

// String manipulation commands
ASTRADB_REGISTER_COMMAND(APPEND, 3, "write", RoutingStrategy::kByFirstKey, HandleAppend);
ASTRADB_REGISTER_COMMAND(STRLEN, 2, "readonly", RoutingStrategy::kByFirstKey, HandleStrlen);
ASTRADB_REGISTER_COMMAND(GETSET, 3, "write", RoutingStrategy::kByFirstKey, HandleGetSet);
ASTRADB_REGISTER_COMMAND(SETEX, 4, "write", RoutingStrategy::kByFirstKey, HandleSetEx);
ASTRADB_REGISTER_COMMAND(SETNX, 3, "write", RoutingStrategy::kByFirstKey, HandleSetNx);
ASTRADB_REGISTER_COMMAND(GETRANGE, 4, "readonly", RoutingStrategy::kByFirstKey, HandleGetRange);
ASTRADB_REGISTER_COMMAND(SETRANGE, 4, "write", RoutingStrategy::kByFirstKey, HandleSetRange);

}  // namespace astra::commands