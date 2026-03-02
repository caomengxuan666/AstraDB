// ==============================================================================
// TTL Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "ttl_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/protocol/resp/resp_builder.hpp"
#include <cstdlib>

namespace astra::commands {

// EXPIRE key seconds
CommandResult HandleExpire(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'EXPIRE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& seconds_arg = command[1];

  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  if (!seconds_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of seconds argument");
  }

  std::string key = key_arg.AsString();
  std::string seconds_str = seconds_arg.AsString();

  char* endptr = nullptr;
  int64_t seconds = std::strtoll(seconds_str.c_str(), &endptr, 10);
  if (endptr == seconds_str.c_str() || *endptr != '\0') {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  if (seconds <= 0) {
    // Negative or zero means delete the key
    if (db->Exists(key)) {
      db->Del(key);
      return CommandResult(RespValue(static_cast<int64_t>(1)));  // 1 = key deleted
    }
    return CommandResult(RespValue(static_cast<int64_t>(0)));  // 0 = key not found
  }

  bool success = db->SetExpireSeconds(key, seconds);
  if (success) {
    return CommandResult(RespValue(static_cast<int64_t>(1)));  // 1 = timeout was set
  } else {
    return CommandResult(RespValue(static_cast<int64_t>(0)));  // 0 = key not found
  }
}

// EXPIREAT key timestamp
CommandResult HandleExpireAt(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'EXPIREAT' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& timestamp_arg = command[1];

  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  if (!timestamp_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of timestamp argument");
  }

  std::string key = key_arg.AsString();
  std::string timestamp_str = timestamp_arg.AsString();

  char* endptr = nullptr;
  int64_t timestamp = std::strtoll(timestamp_str.c_str(), &endptr, 10);
  if (endptr == timestamp_str.c_str() || *endptr != '\0') {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  int64_t current_time_ms = astra::storage::KeyMetadata::GetCurrentTimeMs() / 1000;
  int64_t expire_time_ms = timestamp * 1000;

  if (expire_time_ms <= current_time_ms * 1000) {
    // Timestamp is in the past, delete the key
    if (db->Exists(key)) {
      db->Del(key);
      return CommandResult(RespValue(static_cast<int64_t>(1)));  // 1 = key deleted
    }
    return CommandResult(RespValue(static_cast<int64_t>(0)));  // 0 = key not found
  }

  bool success = db->SetExpireMs(key, expire_time_ms);
  if (success) {
    return CommandResult(RespValue(static_cast<int64_t>(1)));  // 1 = timeout was set
  } else {
    return CommandResult(RespValue(static_cast<int64_t>(0)));  // 0 = key not found
  }
}

// PEXPIRE key milliseconds
CommandResult HandlePExpire(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'PEXPIRE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& ms_arg = command[1];

  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  if (!ms_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of milliseconds argument");
  }

  std::string key = key_arg.AsString();
  std::string ms_str = ms_arg.AsString();

  char* endptr = nullptr;
  int64_t milliseconds = std::strtoll(ms_str.c_str(), &endptr, 10);
  if (endptr == ms_str.c_str() || *endptr != '\0') {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  if (milliseconds <= 0) {
    // Negative or zero means delete the key
    if (db->Exists(key)) {
      db->Del(key);
      return CommandResult(RespValue(static_cast<int64_t>(1)));  // 1 = key deleted
    }
    return CommandResult(RespValue(static_cast<int64_t>(0)));  // 0 = key not found
  }

  // Convert relative time to absolute timestamp
  int64_t expire_time_ms = astra::storage::KeyMetadata::GetCurrentTimeMs() + milliseconds;
  bool success = db->SetExpireMs(key, expire_time_ms);
  if (success) {
    return CommandResult(RespValue(static_cast<int64_t>(1)));  // 1 = timeout was set
  } else {
    return CommandResult(RespValue(static_cast<int64_t>(0)));  // 0 = key not found
  }
}

// PEXPIREAT key timestamp_ms
CommandResult HandlePExpireAt(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'PEXPIREAT' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& timestamp_arg = command[1];

  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  if (!timestamp_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of timestamp argument");
  }

  std::string key = key_arg.AsString();
  std::string timestamp_str = timestamp_arg.AsString();

  char* endptr = nullptr;
  int64_t timestamp_ms = std::strtoll(timestamp_str.c_str(), &endptr, 10);
  if (endptr == timestamp_str.c_str() || *endptr != '\0') {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  int64_t current_time_ms = astra::storage::KeyMetadata::GetCurrentTimeMs();

  if (timestamp_ms <= current_time_ms) {
    // Timestamp is in the past, delete the key
    if (db->Exists(key)) {
      db->Del(key);
      return CommandResult(RespValue(static_cast<int64_t>(1)));  // 1 = key deleted
    }
    return CommandResult(RespValue(static_cast<int64_t>(0)));  // 0 = key not found
  }

  bool success = db->SetExpireMs(key, timestamp_ms);
  if (success) {
    return CommandResult(RespValue(static_cast<int64_t>(1)));  // 1 = timeout was set
  } else {
    return CommandResult(RespValue(static_cast<int64_t>(0)));  // 0 = key not found
  }
}

// TTL key - returns seconds until expiration
CommandResult HandleTTL(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'TTL' command");
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
  int64_t ttl_seconds = db->GetTtlSeconds(key);

  return CommandResult(RespValue(ttl_seconds));
}

// PTTL key - returns milliseconds until expiration
CommandResult HandlePTTL(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'PTTL' command");
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
  int64_t ttl_ms = db->GetTtlMs(key);

  return CommandResult(RespValue(ttl_ms));
}

// PERSIST key - remove expiration
CommandResult HandlePersist(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'PERSIST' command");
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
  bool success = db->Persist(key);

  if (success) {
    return CommandResult(RespValue(static_cast<int64_t>(1)));  // 1 = timeout removed
  } else {
    return CommandResult(RespValue(static_cast<int64_t>(0)));  // 0 = key not found or no timeout
  }
}

// Auto-register all TTL commands
ASTRADB_REGISTER_COMMAND(EXPIRE, 2, "write", RoutingStrategy::kByFirstKey, HandleExpire);
ASTRADB_REGISTER_COMMAND(EXPIREAT, 2, "write", RoutingStrategy::kByFirstKey, HandleExpireAt);
ASTRADB_REGISTER_COMMAND(PEXPIRE, 2, "write", RoutingStrategy::kByFirstKey, HandlePExpire);
ASTRADB_REGISTER_COMMAND(PEXPIREAT, 2, "write", RoutingStrategy::kByFirstKey, HandlePExpireAt);
ASTRADB_REGISTER_COMMAND(TTL, 1, "readonly", RoutingStrategy::kByFirstKey, HandleTTL);
ASTRADB_REGISTER_COMMAND(PTTL, 1, "readonly", RoutingStrategy::kByFirstKey, HandlePTTL);
ASTRADB_REGISTER_COMMAND(PERSIST, 1, "write", RoutingStrategy::kByFirstKey, HandlePersist);

}  // namespace astra::commands