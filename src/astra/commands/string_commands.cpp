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
        int64_t seconds = std::stoll(seconds_arg.AsString());
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
        int64_t millis = std::stoll(millis_arg.AsString());
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
  
  // Log to AOF
  std::vector<std::string> aof_args = {key, value};
  if (expire_time_ms.has_value()) {
    aof_args.push_back("PX");
    aof_args.push_back(std::to_string(*expire_time_ms));
  }
  context->LogToAof("SET", aof_args);
  
  return CommandResult(RespValue(RespType::kSimpleString));
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
  
  // Log to AOF
  context->LogToAof("DEL", keys);
  
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

  // Log to AOF - rebuild command args
  std::vector<std::string> aof_args;
  aof_args.reserve(command.ArgCount());
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    aof_args.push_back(command[i].AsString());
  }
  context->LogToAof("MSET", aof_args);

  return CommandResult(RespValue(RespType::kSimpleString));
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
    if (db->Get(arg.AsString()).has_value()) {
      ++count;
    }
  }

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// Auto-register all string commands
ASTRADB_REGISTER_COMMAND(GET, 1, "readonly", RoutingStrategy::kByFirstKey, HandleGet);
ASTRADB_REGISTER_COMMAND(SET, -2, "write", RoutingStrategy::kByFirstKey, HandleSet);
ASTRADB_REGISTER_COMMAND(DEL, -2, "write", RoutingStrategy::kNone, HandleDel);
ASTRADB_REGISTER_COMMAND(MGET, -2, "readonly", RoutingStrategy::kNone, HandleMGet);
ASTRADB_REGISTER_COMMAND(MSET, -2, "write", RoutingStrategy::kNone, HandleMSet);
ASTRADB_REGISTER_COMMAND(EXISTS, -2, "readonly", RoutingStrategy::kNone, HandleExists);

}  // namespace astra::commands