// ==============================================================================
// String Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "string_commands.hpp"

#include <absl/strings/ascii.h>
#include <absl/strings/numbers.h>

#include <iomanip>
#include <sstream>

#include "astra/protocol/resp/resp_builder.hpp"
#include "command_auto_register.hpp"

namespace astra::commands {

// GET key
CommandResult HandleGet(const astra::protocol::Command& command,
                        CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'GET' command");
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
CommandResult HandleSet(const astra::protocol::Command& command,
                        CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SET' command");
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
          return CommandResult(false,
                               "ERR value is not an integer or out of range");
        }
        if (seconds < 0) {
          return CommandResult(false, "ERR invalid expire time");
        }
        expire_time_ms =
            astra::storage::KeyMetadata::GetCurrentTimeMs() + (seconds * 1000);
      } catch (...) {
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
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
          return CommandResult(false,
                               "ERR value is not an integer or out of range");
        }
        if (millis < 0) {
          return CommandResult(false, "ERR invalid expire time");
        }
        expire_time_ms =
            astra::storage::KeyMetadata::GetCurrentTimeMs() + millis;
      } catch (...) {
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
      }
      ++i;
    } else {
      return CommandResult(false, "ERR syntax error");
    }
  }

  if (nx && xx) {
    return CommandResult(
        false, "ERR NX and XX options at the same time are not compatible");
  }

  // Check if key exists
  bool key_exists = db->Get(key).has_value();

  // Apply NX/XX logic
  if (nx && key_exists) {
    return CommandResult(
        RespValue(RespType::kNullBulkString));  // Already exists
  }
  if (xx && !key_exists) {
    return CommandResult(
        RespValue(RespType::kNullBulkString));  // Does not exist
  }

  // Set the key
  db->Set(key, value);

  // Set expiration if specified
  if (expire_time_ms.has_value()) {
    db->SetExpireMs(key, *expire_time_ms);
  }

  ASTRADB_LOG_DEBUG("HandleSet: About to call LogToAof for key={}", key);

  // Log to AOF (zero-copy with absl::Span)
  if (expire_time_ms.has_value()) {
    std::string px_str = absl::StrCat(*expire_time_ms);
    std::array<absl::string_view, 4> aof_args_with_expire = {key, value, "PX",
                                                             px_str};
    context->LogToAof("SET", absl::MakeSpan(aof_args_with_expire));
  } else {
    std::array<absl::string_view, 2> aof_args_simple = {key, value};
    context->LogToAof("SET", absl::MakeSpan(aof_args_simple));
  }

  ASTRADB_LOG_DEBUG("HandleSet: LogToAof call completed");

  RespValue response;
  response.SetString("OK", RespType::kSimpleString);
  return CommandResult(response);
}

// DEL key [key ...]
CommandResult HandleDel(const astra::protocol::Command& command,
                        CommandContext* context) {
  if (command.ArgCount() == 0) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'DEL' command");
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
CommandResult HandleMGet(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() == 0) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'MGET' command");
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

  absl::InlinedVector<RespValue, 16> array;
  array.reserve(results.size());
  for (const auto& result : results) {
    if (result.has_value()) {
      array.emplace_back(RespValue(std::string(*result)));
    } else {
      array.emplace_back(RespValue(RespType::kNullBulkString));
    }
  }

  return CommandResult(
      RespValue(std::vector<RespValue>(array.begin(), array.end())));
}

// MSET key value [key value ...]
CommandResult HandleMSet(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() == 0 || command.ArgCount() % 2 != 0) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'MSET' command");
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

// MSETNX key value [key value ...]
CommandResult HandleMSetNx(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() == 0 || command.ArgCount() % 2 != 0) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'MSETNX' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  // Check if all keys don't exist
  for (size_t i = 0; i < command.ArgCount(); i += 2) {
    const auto& key_arg = command[i];
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }

    auto existing = db->Get(key_arg.AsString());
    if (existing.has_value()) {
      return CommandResult(RespValue(static_cast<int64_t>(0)));
    }
  }

  // All keys don't exist, set them all
  for (size_t i = 0; i < command.ArgCount(); i += 2) {
    const auto& key_arg = command[i];
    const auto& value_arg = command[i + 1];

    if (!value_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of value argument");
    }

    db->Set(key_arg.AsString(), StringValue(value_arg.AsString()));
  }

  // Log to AOF
  std::vector<absl::string_view> aof_args;
  aof_args.reserve(command.ArgCount());
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    aof_args.emplace_back(command[i].AsString());
  }
  context->LogToAof("MSETNX", absl::MakeSpan(aof_args));

  return CommandResult(RespValue(static_cast<int64_t>(1)));
}

// INCR key
CommandResult HandleIncr(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'INCR' command");
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
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
      }
    } catch (...) {
      return CommandResult(false,
                           "ERR value is not an integer or out of range");
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
CommandResult HandleDecr(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'DECR' command");
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
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
      }
    } catch (...) {
      return CommandResult(false,
                           "ERR value is not an integer or out of range");
    }
  }

  int_value--;
  db->Set(key, absl::StrCat(int_value));

  std::array<absl::string_view, 1> aof_args = {key};
  context->LogToAof("DECR", aof_args);

  return CommandResult(RespValue(int_value));
}

// INCRBY key increment
CommandResult HandleIncrBy(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'INCRBY' command");
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
      return CommandResult(false,
                           "ERR value is not an integer or out of range");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  auto value = db->Get(key);

  int64_t int_value = 0;
  if (value.has_value()) {
    try {
      if (!absl::SimpleAtoi(value->value, &int_value)) {
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
      }
    } catch (...) {
      return CommandResult(false,
                           "ERR value is not an integer or out of range");
    }
  }

  int_value += increment;
  db->Set(key, absl::StrCat(int_value));

  std::string incr_str = incr_arg.AsString();
  std::array<absl::string_view, 2> aof_args = {key, incr_str};
  context->LogToAof("INCRBY", aof_args);

  return CommandResult(RespValue(int_value));
}

// DECRBY key decrement
CommandResult HandleDecrBy(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'DECRBY' command");
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
      return CommandResult(false,
                           "ERR value is not an integer or out of range");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  auto value = db->Get(key);

  int64_t int_value = 0;
  if (value.has_value()) {
    try {
      if (!absl::SimpleAtoi(value->value, &int_value)) {
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
      }
    } catch (...) {
      return CommandResult(false,
                           "ERR value is not an integer or out of range");
    }
  }

  int_value -= decrement;
  db->Set(key, absl::StrCat(int_value));

  std::string decr_str = decr_arg.AsString();
  std::array<absl::string_view, 2> aof_args = {key, decr_str};
  context->LogToAof("DECRBY", aof_args);

  return CommandResult(RespValue(int_value));
}

// APPEND key value
CommandResult HandleAppend(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'APPEND' command");
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
CommandResult HandleStrlen(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'STRLEN' command");
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
CommandResult HandleGetSet(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'GETSET' command");
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
CommandResult HandleSetEx(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SETEX' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& seconds_arg = command[1];
  const auto& value_arg = command[2];

  if (!key_arg.IsBulkString() || !seconds_arg.IsBulkString() ||
      !value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string value = value_arg.AsString();

  int64_t seconds;
  try {
    if (!absl::SimpleAtoi(seconds_arg.AsString(), &seconds)) {
      return CommandResult(false,
                           "ERR value is not an integer or out of range");
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
  std::string seconds_str = seconds_arg.AsString();
  std::array<absl::string_view, 3> aof_args = {key, seconds_str, value};
  context->LogToAof("SETEX", aof_args);

  RespValue response;
  response.SetString("OK", RespType::kSimpleString);
  return CommandResult(response);
}

// SETNX key value
CommandResult HandleSetNx(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SETNX' command");
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
CommandResult HandleGetRange(const astra::protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'GETRANGE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& start_arg = command[1];
  const auto& end_arg = command[2];

  if (!key_arg.IsBulkString() || !start_arg.IsBulkString() ||
      !end_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  int64_t start, end;

  try {
    if (!absl::SimpleAtoi(start_arg.AsString(), &start)) {
      return CommandResult(false,
                           "ERR value is not an integer or out of range");
    }
    if (!absl::SimpleAtoi(end_arg.AsString(), &end)) {
      return CommandResult(false,
                           "ERR value is not an integer or out of range");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  std::string result = db->GetRange(key, start, end);
  return CommandResult(RespValue(std::move(result)));
}

// SETRANGE key offset value
CommandResult HandleSetRange(const astra::protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'SETRANGE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& offset_arg = command[1];
  const auto& value_arg = command[2];

  if (!key_arg.IsBulkString() || !offset_arg.IsBulkString() ||
      !value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string value = value_arg.AsString();
  int64_t offset;

  try {
    if (!absl::SimpleAtoi(offset_arg.AsString(), &offset)) {
      return CommandResult(false,
                           "ERR value is not an integer or out of range");
    }
    if (offset < 0) {
      return CommandResult(false, "ERR offset is out of range");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  size_t new_len = db->SetRange(key, offset, value);

  // Log to AOF
  std::string offset_str = offset_arg.AsString();
  std::array<absl::string_view, 3> aof_args = {key, offset_str, value};
  context->LogToAof("SETRANGE", aof_args);

  return CommandResult(RespValue(static_cast<int64_t>(new_len)));
}

// STRALGO ALGORITHM ... - String algorithm command (Redis 6.0+)
// Simplified implementation for LCS algorithm
CommandResult HandleStrAlgo(const astra::protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'STRALGO' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& algo_arg = command[0];
  if (!algo_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of algorithm argument");
  }

  std::string algo = algo_arg.AsString();

  if (algo == "LCS") {
    // LCS key1 key2 [LEN] [IDX] [MINMATCHLEN len] [WITHMATCHLEN len]
    // Simplified: just return LCS string between two keys
    if (command.ArgCount() < 3) {
      return CommandResult(false,
                           "ERR wrong number of arguments for 'STRALGO LCS'");
    }

    const auto& key1_arg = command[1];
    const auto& key2_arg = command[2];

    if (!key1_arg.IsBulkString() || !key2_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }

    auto str1_opt = db->Get(key1_arg.AsString());
    auto str2_opt = db->Get(key2_arg.AsString());

    if (!str1_opt || !str2_opt) {
      return CommandResult(
          RespValue(""));  // Return empty string if either key doesn't exist
    }

    // Simple LCS implementation (can be optimized)
    const std::string& str1 = str1_opt->value;
    const std::string& str2 = str2_opt->value;

    // Parse options
    bool return_len = false;
    bool return_idx = false;
    for (size_t i = 3; i < command.ArgCount(); ++i) {
      if (command[i].IsBulkString()) {
        std::string opt = command[i].AsString();
        if (opt == "LEN") {
          return_len = true;
        } else if (opt == "IDX") {
          return_idx = true;
        }
      }
    }

    if (return_len || return_idx) {
      // For now, only support simple LCS string return
      return CommandResult(false,
                           "ERR STRALGO LCS IDX/LEN not implemented yet");
    }

    // Compute LCS using dynamic programming
    size_t m = str1.length();
    size_t n = str2.length();
    std::vector<std::vector<size_t>> dp(m + 1, std::vector<size_t>(n + 1, 0));

    for (size_t i = 1; i <= m; ++i) {
      for (size_t j = 1; j <= n; ++j) {
        if (str1[i - 1] == str2[j - 1]) {
          dp[i][j] = dp[i - 1][j - 1] + 1;
        } else {
          dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
        }
      }
    }

    // Backtrack to find LCS string
    std::string lcs;
    size_t i = m, j = n;
    while (i > 0 && j > 0) {
      if (str1[i - 1] == str2[j - 1]) {
        lcs = str1[i - 1] + lcs;
        --i;
        --j;
      } else if (dp[i - 1][j] > dp[i][j - 1]) {
        --i;
      } else {
        --j;
      }
    }

    return CommandResult(RespValue(lcs));
  } else {
    return CommandResult(false, "ERR unknown algorithm for STRALGO");
  }
}

// EXISTS key [key ...]
CommandResult HandleExists(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() == 0) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'EXISTS' command");
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

// COPY source destination [DB destination-db] [REPLACE]
CommandResult HandleCopy(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'COPY' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& source_arg = command[0];
  const auto& dest_arg = command[1];

  if (!source_arg.IsBulkString() || !dest_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string source = source_arg.AsString();
  std::string destination = dest_arg.AsString();

  // Check if source exists
  auto source_value = db->Get(source);
  if (!source_value.has_value()) {
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  }

  // Parse options
  bool replace = false;
  for (size_t i = 2; i < command.ArgCount(); ++i) {
    if (command[i].IsBulkString()) {
      std::string opt = command[i].AsString();
      if (opt == "REPLACE") {
        replace = true;
      } else if (opt == "DB") {
        // Skip DB argument and its value
        i++;
      }
    }
  }

  // Check if destination exists (if not replace)
  if (!replace) {
    auto dest_value = db->Get(destination);
    if (dest_value.has_value()) {
      return CommandResult(RespValue(static_cast<int64_t>(0)));
    }
  }

  // Copy the value
  db->Set(destination, *source_value);

  return CommandResult(RespValue(static_cast<int64_t>(1)));
}

// DUMP key - Serialize value
CommandResult HandleDump(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'DUMP' command");
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

  // Get value
  auto value = db->Get(key);
  if (!value.has_value()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  // Simplified serialization: version + type + value
  std::string serialized = "1:" + value->value;

  RespValue result;
  result.SetString(serialized, RespType::kBulkString);
  return CommandResult(result);
}

// RESTORE key ttl serialized-value [REPLACE]
CommandResult HandleRestore(const astra::protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'RESTORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& ttl_arg = command[1];
  const auto& value_arg = command[2];

  if (!key_arg.IsBulkString() || !ttl_arg.IsBulkString() ||
      !value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string ttl_str = ttl_arg.AsString();
  std::string serialized = value_arg.AsString();

  // Parse TTL
  int64_t ttl;
  if (!absl::SimpleAtoi(ttl_str, &ttl) || ttl < 0) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  // Parse serialized value (simplified: "1:value")
  std::string value = serialized.substr(2);

  // Parse options
  bool replace = false;
  for (size_t i = 3; i < command.ArgCount(); ++i) {
    if (command[i].IsBulkString()) {
      std::string opt = command[i].AsString();
      if (opt == "REPLACE") {
        replace = true;
      }
    }
  }

  // Check if destination exists (if not replace)
  if (!replace) {
    auto dest_value = db->Get(key);
    if (dest_value.has_value()) {
      return CommandResult(false, "BUSYKEY Target key name already exists.");
    }
  }

  // Restore value
  db->Set(key, StringValue(value));

  // Set TTL if needed
  if (ttl > 0) {
    db->SetExpireSeconds(key, ttl / 1000);
  }

  RespValue response;
  response.SetString("OK", RespType::kSimpleString);
  return CommandResult(response);
}

// UNLINK key [key ...] - Delete keys asynchronously
CommandResult HandleUnlink(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'UNLINK' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  std::vector<std::string> keys;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& key_arg = command[i];
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(key_arg.AsString());
  }

  // For now, UNLINK behaves like DEL (can be optimized for async deletion)
  size_t deleted = db->Del(keys);

  return CommandResult(RespValue(static_cast<int64_t>(deleted)));
}

// GETDEL key - Get the value and delete the key
CommandResult HandleGetDel(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'GETDEL' command");
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

  // Get value
  auto value = db->Get(key);
  if (!value.has_value()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  // Delete the key
  db->Del(key);

  // Return the value
  RespValue result;
  result.SetString(value->value, RespType::kBulkString);
  return CommandResult(result);
}

// GETEX key [EX seconds | PX milliseconds | EXAT unix-time-seconds | PXAT
// unix-time-milliseconds | PERSIST]
CommandResult HandleGetEx(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'GETEX' command");
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

  // Get value
  auto value = db->Get(key);
  if (!value.has_value()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  // Parse options
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    std::string option = absl::AsciiStrToUpper(command[i].AsString());

    if (option == "EX") {
      // EX seconds
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR syntax error");
      }
      int64_t seconds;
      if (!absl::SimpleAtoi(command[++i].AsString(), &seconds) ||
          seconds <= 0) {
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
      }
      db->SetExpireSeconds(key, seconds);
    } else if (option == "PX") {
      // PX milliseconds
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR syntax error");
      }
      int64_t milliseconds;
      if (!absl::SimpleAtoi(command[++i].AsString(), &milliseconds) ||
          milliseconds <= 0) {
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
      }
      db->SetExpireMs(
          key, astra::storage::KeyMetadata::GetCurrentTimeMs() + milliseconds);
    } else if (option == "EXAT") {
      // EXAT unix-time-seconds
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR syntax error");
      }
      int64_t timestamp;
      if (!absl::SimpleAtoi(command[++i].AsString(), &timestamp) ||
          timestamp <= 0) {
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
      }
      db->SetExpireMs(key, timestamp * 1000);
    } else if (option == "PXAT") {
      // PXAT unix-time-milliseconds
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR syntax error");
      }
      int64_t timestamp;
      if (!absl::SimpleAtoi(command[++i].AsString(), &timestamp) ||
          timestamp <= 0) {
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
      }
      db->SetExpireMs(key, timestamp);
    } else if (option == "PERSIST") {
      // Remove expiration
      db->Persist(key);
    } else {
      return CommandResult(false, "ERR syntax error");
    }
  }

  // Return the value
  RespValue result;
  result.SetString(value->value, RespType::kBulkString);
  return CommandResult(result);
}

// ECHO message
CommandResult HandleEcho(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ECHO' command");
  }

  const auto& message_arg = command[0];

  if (!message_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of message argument");
  }

  std::string message = message_arg.AsString();

  RespValue result;
  result.SetString(message, RespType::kBulkString);
  return CommandResult(result);
}

// INCRBYFLOAT key increment
CommandResult HandleIncrByFloat(const astra::protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'INCRBYFLOAT' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& increment_arg = command[1];

  if (!key_arg.IsBulkString() || !increment_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string increment_str = increment_arg.AsString();

  // Parse increment as double
  double increment;
  if (!absl::SimpleAtod(increment_str, &increment)) {
    return CommandResult(false, "ERR value is not a valid float");
  }

  // Check if key exists and has valid value
  auto value = db->Get(key);
  double current_value = 0.0;

  if (value.has_value()) {
    if (!absl::SimpleAtod(value->value, &current_value)) {
      return CommandResult(false, "ERR value is not a valid float");
    }
  }

  // Perform increment
  double new_value = current_value + increment;

  // Format result without trailing zeros
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(17) << new_value;
  std::string result_str = oss.str();

  // Remove trailing zeros
  size_t dot_pos = result_str.find('.');
  if (dot_pos != std::string::npos) {
    size_t last_non_zero = result_str.find_last_not_of('0');
    if (last_non_zero != std::string::npos && last_non_zero > dot_pos) {
      result_str = result_str.substr(0, last_non_zero + 1);
    }
    // Remove trailing dot
    if (result_str.back() == '.') {
      result_str.pop_back();
    }
  }

  db->Set(key, result_str);

  RespValue result;
  result.SetString(result_str, RespType::kBulkString);
  return CommandResult(result);
}

// PSETEX key milliseconds value
CommandResult HandlePSetEx(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'PSETEX' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& ms_arg = command[1];
  const auto& value_arg = command[2];

  if (!key_arg.IsBulkString() || !ms_arg.IsBulkString() ||
      !value_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string ms_str = ms_arg.AsString();
  std::string value = value_arg.AsString();

  // Parse milliseconds
  int64_t milliseconds;
  if (!absl::SimpleAtoi(ms_str, &milliseconds) || milliseconds <= 0) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  // Set value and expiration
  db->Set(key, value);
  db->SetExpireMs(
      key, astra::storage::KeyMetadata::GetCurrentTimeMs() + milliseconds);

  RespValue result;
  result.SetString("OK", RespType::kSimpleString);
  return CommandResult(result);
}

// SUBSTR key start end (deprecated, same as GETRANGE)
CommandResult HandleSubstr(const astra::protocol::Command& command,
                           CommandContext* context) {
  // SUBSTR is just an alias for GETRANGE
  return HandleGetRange(command, context);
}

// LCS key1 key2 [LEN]
CommandResult HandleLcs(const astra::protocol::Command& command,
                        CommandContext* context) {
  if (command.ArgCount() < 2 || command.ArgCount() > 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'LCS' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key1_arg = command[0];
  const auto& key2_arg = command[1];

  if (!key1_arg.IsBulkString() || !key2_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key1 = key1_arg.AsString();
  std::string key2 = key2_arg.AsString();

  auto value1 = db->Get(key1);
  auto value2 = db->Get(key2);

  if (!value1.has_value() || !value2.has_value()) {
    return CommandResult(RespValue());
  }

  std::string str1 = value1->value;
  std::string str2 = value2->value;

  // Check if LEN option is provided
  bool len_only = false;
  if (command.ArgCount() == 3) {
    std::string option = absl::AsciiStrToUpper(command[2].AsString());
    if (option == "LEN") {
      len_only = true;
    } else {
      return CommandResult(false, "ERR unknown option '" + option + "'");
    }
  }

  // Compute Longest Common Subsequence using dynamic programming
  int m = str1.length();
  int n = str2.length();

  // DP table to store lengths of LCS of substrings
  std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));

  for (int i = 1; i <= m; i++) {
    for (int j = 1; j <= n; j++) {
      if (str1[i - 1] == str2[j - 1]) {
        dp[i][j] = dp[i - 1][j - 1] + 1;
      } else {
        dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
      }
    }
  }

  int lcs_length = dp[m][n];

  if (len_only) {
    return CommandResult(RespValue(static_cast<int64_t>(lcs_length)));
  }

  // Backtrack to find the LCS string
  std::string lcs;
  int i = m, j = n;
  while (i > 0 && j > 0) {
    if (str1[i - 1] == str2[j - 1]) {
      lcs.push_back(str1[i - 1]);
      i--;
      j--;
    } else if (dp[i - 1][j] > dp[i][j - 1]) {
      i--;
    } else {
      j--;
    }
  }

  // Reverse the LCS string since we built it backwards
  std::reverse(lcs.begin(), lcs.end());

  RespValue result;
  result.SetString(lcs, RespType::kBulkString);
  return CommandResult(result);
}

// Auto-register all string commands
ASTRADB_REGISTER_COMMAND(GET, 2, "readonly,fast", RoutingStrategy::kByFirstKey,
                         HandleGet);
ASTRADB_REGISTER_COMMAND(SET, -3, "write", RoutingStrategy::kByFirstKey,
                         HandleSet);
ASTRADB_REGISTER_COMMAND(DEL, -2, "write", RoutingStrategy::kByFirstKey,
                         HandleDel);
ASTRADB_REGISTER_COMMAND(MGET, -2, "readonly", RoutingStrategy::kNone,
                         HandleMGet);
ASTRADB_REGISTER_COMMAND(MSET, -3, "write", RoutingStrategy::kNone, HandleMSet);
ASTRADB_REGISTER_COMMAND(MSETNX, -3, "write", RoutingStrategy::kNone,
                         HandleMSetNx);
ASTRADB_REGISTER_COMMAND(EXISTS, -2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleExists);

// Increment/Decrement commands
ASTRADB_REGISTER_COMMAND(INCR, 2, "write", RoutingStrategy::kByFirstKey,
                         HandleIncr);
ASTRADB_REGISTER_COMMAND(DECR, 2, "write", RoutingStrategy::kByFirstKey,
                         HandleDecr);
ASTRADB_REGISTER_COMMAND(INCRBY, 3, "write", RoutingStrategy::kByFirstKey,
                         HandleIncrBy);
ASTRADB_REGISTER_COMMAND(DECRBY, 3, "write", RoutingStrategy::kByFirstKey,
                         HandleDecrBy);
ASTRADB_REGISTER_COMMAND(INCRBYFLOAT, 3, "write", RoutingStrategy::kByFirstKey,
                         HandleIncrByFloat);

// String manipulation commands
ASTRADB_REGISTER_COMMAND(APPEND, 3, "write", RoutingStrategy::kByFirstKey,
                         HandleAppend);
ASTRADB_REGISTER_COMMAND(STRLEN, 2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleStrlen);
ASTRADB_REGISTER_COMMAND(GETSET, 3, "write", RoutingStrategy::kByFirstKey,
                         HandleGetSet);
ASTRADB_REGISTER_COMMAND(SETEX, 4, "write", RoutingStrategy::kByFirstKey,
                         HandleSetEx);
ASTRADB_REGISTER_COMMAND(SETNX, 3, "write", RoutingStrategy::kByFirstKey,
                         HandleSetNx);
ASTRADB_REGISTER_COMMAND(PSETEX, 4, "write", RoutingStrategy::kByFirstKey,
                         HandlePSetEx);
ASTRADB_REGISTER_COMMAND(GETRANGE, 4, "readonly", RoutingStrategy::kByFirstKey,
                         HandleGetRange);
ASTRADB_REGISTER_COMMAND(SETRANGE, 4, "write", RoutingStrategy::kByFirstKey,
                         HandleSetRange);
ASTRADB_REGISTER_COMMAND(STRALGO, -2, "readonly", RoutingStrategy::kNone,
                         HandleStrAlgo);
ASTRADB_REGISTER_COMMAND(SUBSTR, 4, "readonly", RoutingStrategy::kByFirstKey,
                         HandleSubstr);
ASTRADB_REGISTER_COMMAND(ECHO, 2, "readonly", RoutingStrategy::kNone,
                         HandleEcho);
ASTRADB_REGISTER_COMMAND(LCS, -3, "readonly", RoutingStrategy::kByFirstKey,
                         HandleLcs);

// Advanced string commands (Redis 6.0+)
ASTRADB_REGISTER_COMMAND(COPY, -3, "write", RoutingStrategy::kByFirstKey,
                         HandleCopy);
ASTRADB_REGISTER_COMMAND(DUMP, 2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleDump);
ASTRADB_REGISTER_COMMAND(RESTORE, -3, "write", RoutingStrategy::kByFirstKey,
                         HandleRestore);
ASTRADB_REGISTER_COMMAND(UNLINK, -2, "write", RoutingStrategy::kByFirstKey,
                         HandleUnlink);
ASTRADB_REGISTER_COMMAND(GETDEL, 2, "write", RoutingStrategy::kByFirstKey,
                         HandleGetDel);
ASTRADB_REGISTER_COMMAND(GETEX, -2, "write", RoutingStrategy::kByFirstKey,
                         HandleGetEx);

}  // namespace astra::commands
