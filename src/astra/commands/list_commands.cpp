// ==============================================================================
// List Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "list_commands.hpp"
#include "command_auto_register.hpp"
#include "blocking_manager.hpp"
#include "astra/protocol/resp/resp_builder.hpp"
#include "astra/network/connection.hpp"
#include <algorithm>
#include <sstream>
#include <chrono>

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

  // Wake up blocked clients waiting on this key
  auto* blocking_manager = context->GetBlockingManager();
  if (blocking_manager) {
    blocking_manager->WakeUpBlockedClients(key);
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

  // Wake up blocked clients waiting on this key
  auto* blocking_manager = context->GetBlockingManager();
  if (blocking_manager) {
    blocking_manager->WakeUpBlockedClients(key);
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

// BLPOP key [key ...] timeout
// Note: This is a simplified implementation without real blocking.
// TODO: Implement real blocking with wait queues and timeout management
// TODO: Implement client notification when data becomes available
// TODO: Track blocked clients and wake them up when data is pushed
CommandResult HandleBLPop(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'BLPOP' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  // Parse timeout (last argument)
  const auto& timeout_arg = command[command.ArgCount() - 1];
  double timeout = 0.0;
  
  if (timeout_arg.IsInteger()) {
    timeout = static_cast<double>(timeout_arg.AsInteger());
  } else if (timeout_arg.IsBulkString()) {
    try {
      timeout = std::stod(timeout_arg.AsString());
    } catch (...) {
      return CommandResult(false, "ERR timeout is not a float or out of range");
    }
  } else {
    return CommandResult(false, "ERR timeout is not a float or out of range");
  }

  // Check keys in order
  for (size_t i = 0; i < command.ArgCount() - 1; ++i) {
    const auto& key_arg = command[i];
    
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }

    std::string key = key_arg.AsString();
    
    // Try to pop from this key
    auto value = db->LPop(key);
    if (value.has_value()) {
      // Return [key, value] array
      std::vector<RespValue> result;
      result.push_back(RespValue(key));
      result.push_back(RespValue(*value));
      return CommandResult(RespValue(std::move(result)));
    }
  }

  // All lists are empty, implement blocking
  if (timeout > 0) {
    // Get blocking manager
    auto* blocking_manager = context->GetBlockingManager();
    if (!blocking_manager) {
      // Blocking manager not available, return nil
      return CommandResult(RespValue(RespType::kNullBulkString));
    }
    
    // Get connection ID
    uint64_t client_id = context->GetConnectionId();
    
    // Debug: check command[0] type and value
    ASTRADB_LOG_DEBUG("BLPOP blocking: command[0] type={}, value='{}'", 
                     static_cast<int>(command[0].GetType()), 
                     command[0].AsString());
    
    // Create blocked client with callback
    BlockedClient blocked_client;
    blocked_client.client_id = client_id;
    blocked_client.key = command[0].AsString();  // Use first key
    blocked_client.command = command;
    blocked_client.timeout_seconds = timeout;
    blocked_client.start_time = std::chrono::steady_clock::now();
    blocked_client.connection = context->GetConnection();  // Save connection pointer
    
    // Debug: check blocked_client.key after assignment
    ASTRADB_LOG_DEBUG("BLPOP: blocked_client.key='{}'", blocked_client.key);
    
    // Set callback to execute LPop when woken up
    // The notification parameter contains information about why we were woken up
    blocked_client.callback = [db, key = command[0].AsString()](const RespValue& notification) -> RespValue {
      ASTRADB_LOG_DEBUG("BLPOP callback invoked for key='{}'", key);
      // When woken up, try to pop from the list
      auto value = db->LPop(key);
      if (value.has_value()) {
        // Return [key, value] array
        std::vector<RespValue> result;
        result.push_back(RespValue(key));
        result.push_back(RespValue(*value));
        ASTRADB_LOG_DEBUG("BLPOP callback: Got value for key='{}'", key);
        return RespValue(std::move(result));
      } else {
        // List still empty
        ASTRADB_LOG_DEBUG("BLPOP callback: List still empty for key='{}'", key);
        return RespValue(RespType::kNullBulkString);
      }
    };
    
    // Add to blocking queue
    // Important: Copy key before moving blocked_client to avoid moving the key
    std::string key_copy = blocked_client.key;
    ASTRADB_LOG_DEBUG("BLPOP: About to call AddBlockedClient with key='{}'", key_copy);
    blocking_manager->AddBlockedClient(key_copy, std::move(blocked_client));
    
    // Return blocking result (response will be sent later)
    ASTRADB_LOG_DEBUG("BLPOP: Returning blocking result");
    return CommandResult::Blocking();
  }
  
  // Non-blocking mode, return nil
  ASTRADB_LOG_DEBUG("BLPOP: Returning nil (non-blocking mode)");
  return CommandResult(RespValue(RespType::kNullBulkString));
}

// BRPOP key [key ...] timeout
// Note: This is a simplified implementation without real blocking.
// TODO: Implement real blocking with wait queues and timeout management
// TODO: Implement client notification when data becomes available
// TODO: Track blocked clients and wake them up when data is pushed
CommandResult HandleBRPop(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'BRPOP' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  // Parse timeout (last argument) - will be used for blocking implementation
  const auto& timeout_arg = command[command.ArgCount() - 1];
  [[maybe_unused]] double timeout = 0.0;
  
  if (timeout_arg.IsInteger()) {
    timeout = static_cast<double>(timeout_arg.AsInteger());
  } else if (timeout_arg.IsBulkString()) {
    try {
      timeout = std::stod(timeout_arg.AsString());
    } catch (...) {
      return CommandResult(false, "ERR timeout is not a float or out of range");
    }
  } else {
    return CommandResult(false, "ERR timeout is not a float or out of range");
  }

  // Check keys in order
  for (size_t i = 0; i < command.ArgCount() - 1; ++i) {
    const auto& key_arg = command[i];
    
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }

    std::string key = key_arg.AsString();
    
    // Try to pop from this key
    auto value = db->RPop(key);
    if (value.has_value()) {
      // Return [key, value] array
      std::vector<RespValue> result;
      result.push_back(RespValue(key));
      result.push_back(RespValue(*value));
      return CommandResult(RespValue(std::move(result)));
    }
  }

  // All lists are empty, implement blocking
  if (timeout > 0) {
    // Get blocking manager
    auto* blocking_manager = context->GetBlockingManager();
    if (!blocking_manager) {
      // Blocking manager not available, return nil
      return CommandResult(RespValue(RespType::kNullBulkString));
    }
    
    // Get connection ID
    uint64_t client_id = context->GetConnectionId();
    
    // Create blocked client with callback
    BlockedClient blocked_client;
    blocked_client.client_id = client_id;
    blocked_client.key = command[0].AsString();  // Use first key
    blocked_client.command = command;
    blocked_client.timeout_seconds = timeout;
    blocked_client.start_time = std::chrono::steady_clock::now();
    blocked_client.connection = context->GetConnection();  // Save connection pointer
    
    // Set callback to execute RPop when woken up
    blocked_client.callback = [db, key = command[0].AsString()](const RespValue& notification) -> RespValue {
      // When woken up, try to pop from the list
      auto value = db->RPop(key);
      if (value.has_value()) {
        // Return [key, value] array
        std::vector<RespValue> result;
        result.push_back(RespValue(key));
        result.push_back(RespValue(*value));
        return RespValue(std::move(result));
      } else {
        // List still empty, send nil
        return RespValue(RespType::kNullBulkString);
      }
    };
    
    // Add to blocking queue
    blocking_manager->AddBlockedClient(blocked_client.key, std::move(blocked_client));
    
    // Return blocking result (response will be sent later)
    return CommandResult::Blocking();
  }
  
  // Non-blocking mode, return nil
  return CommandResult(RespValue(RespType::kNullBulkString));
}

// BRPOPLPUSH source destination timeout
// Note: This is a simplified implementation without real blocking.
// TODO: Implement real blocking with wait queues and timeout management
// TODO: Implement client notification when data becomes available
// TODO: Track blocked clients and wake them up when data is pushed
CommandResult HandleBRPopLPush(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'BRPOPLPUSH' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& source_arg = command[0];
  const auto& dest_arg = command[1];
  const auto& timeout_arg = command[2];

  if (!source_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of source argument");
  }

  if (!dest_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of destination argument");
  }

  // Parse timeout - will be used for blocking implementation
  [[maybe_unused]] double timeout = 0.0;
  if (timeout_arg.IsInteger()) {
    timeout = static_cast<double>(timeout_arg.AsInteger());
  } else if (timeout_arg.IsBulkString()) {
    try {
      timeout = std::stod(timeout_arg.AsString());
    } catch (...) {
      return CommandResult(false, "ERR timeout is not a float or out of range");
    }
  } else {
    return CommandResult(false, "ERR timeout is not a float or out of range");
  }

  std::string source = source_arg.AsString();
  std::string dest = dest_arg.AsString();

  // Try to pop and push
  auto value = db->RPopLPush(source, dest);

  if (!value.has_value()) {
    // Source list is empty, return nil
    // TODO: Implement real blocking with timeout and wait queues
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  return CommandResult(RespValue(*value));
}

// BLMOVE source destination WHERE_FROM WHERE_TO timeout
// Note: This is a simplified implementation without real blocking.
// TODO: Implement real blocking with wait queues and timeout management
// TODO: Implement client notification when data becomes available
// TODO: Track blocked clients and wake them up when data is pushed
CommandResult HandleBLMove(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 5) {
    return CommandResult(false, "ERR wrong number of arguments for 'BLMOVE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& source_arg = command[0];
  const auto& dest_arg = command[1];
  const auto& from_arg = command[2];
  const auto& to_arg = command[3];
  const auto& timeout_arg = command[4];

  if (!source_arg.IsBulkString() || !dest_arg.IsBulkString() ||
      !from_arg.IsBulkString() || !to_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of arguments for 'BLMOVE' command");
  }

  // Parse direction
  std::string from = from_arg.AsString();
  std::string to = to_arg.AsString();
  
  if (from != "LEFT" && from != "RIGHT") {
    return CommandResult(false, "ERR syntax error");
  }
  
  if (to != "LEFT" && to != "RIGHT") {
    return CommandResult(false, "ERR syntax error");
  }

  // Parse timeout - will be used for blocking implementation
  [[maybe_unused]] double timeout = 0.0;
  if (timeout_arg.IsInteger()) {
    timeout = static_cast<double>(timeout_arg.AsInteger());
  } else if (timeout_arg.IsBulkString()) {
    try {
      timeout = std::stod(timeout_arg.AsString());
    } catch (...) {
      return CommandResult(false, "ERR timeout is not a float or out of range");
    }
  } else {
    return CommandResult(false, "ERR timeout is not a float or out of range");
  }

  std::string source = source_arg.AsString();
  std::string dest = dest_arg.AsString();

  // Perform the move operation
  std::optional<std::string> value;
  
  if (from == "LEFT" && to == "LEFT") {
    // LPOP then LPUSH
    value = db->LPop(source);
    if (value.has_value()) {
      db->LPush(dest, *value);
    }
  } else if (from == "LEFT" && to == "RIGHT") {
    // LPOP then RPUSH
    value = db->LPop(source);
    if (value.has_value()) {
      db->RPush(dest, *value);
    }
  } else if (from == "RIGHT" && to == "LEFT") {
    // RPOP then LPUSH
    value = db->RPop(source);
    if (value.has_value()) {
      db->LPush(dest, *value);
    }
  } else if (from == "RIGHT" && to == "RIGHT") {
    // RPOP then RPUSH
    value = db->RPop(source);
    if (value.has_value()) {
      db->RPush(dest, *value);
    }
  }

  if (!value.has_value()) {
    // Source list is empty, return nil
    // TODO: Implement real blocking with timeout and wait queues
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  return CommandResult(RespValue(*value));
}

// BLMPOP timeout key [key ...] count [LEFT|RIGHT]
// Note: This is a simplified implementation without real blocking.
// TODO: Implement real blocking with wait queues and timeout management
// TODO: Implement client notification when data becomes available
// TODO: Track blocked clients and wake them up when data is pushed
CommandResult HandleBLMPop(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'BLMPOP' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  // Parse timeout (first argument) - will be used for blocking implementation
  const auto& timeout_arg = command[0];
  [[maybe_unused]] double timeout = 0.0;
  
  if (timeout_arg.IsInteger()) {
    timeout = static_cast<double>(timeout_arg.AsInteger());
  } else if (timeout_arg.IsBulkString()) {
    try {
      timeout = std::stod(timeout_arg.AsString());
    } catch (...) {
      return CommandResult(false, "ERR timeout is not a float or out of range");
    }
  } else {
    return CommandResult(false, "ERR timeout is not a float or out of range");
  }

  // Parse count (second-to-last argument)
  size_t count_idx = command.ArgCount() - 2;
  const auto& count_arg = command[count_idx];
  int64_t count = 1;
  
  if (count_arg.IsInteger()) {
    count = count_arg.AsInteger();
  } else if (count_arg.IsBulkString()) {
    try {
      count = std::stoll(count_arg.AsString());
    } catch (...) {
      return CommandResult(false, "ERR count is not an integer or out of range");
    }
  }
  
  if (count < 1) {
    return CommandResult(false, "ERR count must be positive");
  }

  // Parse direction (last argument, optional, default LEFT)
  std::string direction = "LEFT";
  const auto& dir_arg = command[command.ArgCount() - 1];
  
  if (dir_arg.IsBulkString()) {
    std::string dir = dir_arg.AsString();
    if (dir == "LEFT" || dir == "RIGHT") {
      direction = dir;
    }
  }

  // Try to pop from each key
  for (size_t i = 1; i < count_idx; ++i) {
    const auto& key_arg = command[i];
    
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }

    std::string key = key_arg.AsString();
    
    // Pop elements based on direction
    std::vector<std::string> values;
    bool popped = false;
    
    for (int64_t j = 0; j < count; ++j) {
      if (direction == "LEFT") {
        auto value = db->LPop(key);
        if (value.has_value()) {
          values.push_back(*value);
          popped = true;
        } else {
          break;  // No more elements in this list
        }
      } else {
        auto value = db->RPop(key);
        if (value.has_value()) {
          values.push_back(*value);
          popped = true;
        } else {
          break;  // No more elements in this list
        }
      }
    }
    
    if (popped) {
      // Return [key, [value1, value2, ...]]
      std::vector<RespValue> result;
      result.push_back(RespValue(key));
      
      std::vector<RespValue> value_array;
      for (const auto& val : values) {
        value_array.push_back(RespValue(val));
      }
      result.push_back(RespValue(std::move(value_array)));
      
      return CommandResult(RespValue(std::move(result)));
    }
  }

  // All lists are empty, return nil
  // TODO: Implement real blocking with timeout and wait queues
  return CommandResult(RespValue(RespType::kNullBulkString));
}

// Auto-register all list commands
ASTRADB_REGISTER_COMMAND(LPUSH, -3, "write", RoutingStrategy::kByFirstKey, HandleLPush);
ASTRADB_REGISTER_COMMAND(RPUSH, -3, "write", RoutingStrategy::kByFirstKey, HandleRPush);
ASTRADB_REGISTER_COMMAND(LPOP, 2, "write", RoutingStrategy::kByFirstKey, HandleLPop);
ASTRADB_REGISTER_COMMAND(RPOP, 2, "write", RoutingStrategy::kByFirstKey, HandleRPop);
ASTRADB_REGISTER_COMMAND(LLEN, 2, "readonly", RoutingStrategy::kByFirstKey, HandleLLen);
ASTRADB_REGISTER_COMMAND(LINDEX, 3, "readonly", RoutingStrategy::kByFirstKey, HandleLIndex);
ASTRADB_REGISTER_COMMAND(LSET, 4, "write", RoutingStrategy::kByFirstKey, HandleLSet);
ASTRADB_REGISTER_COMMAND(LRANGE, 4, "readonly", RoutingStrategy::kByFirstKey, HandleLRange);
ASTRADB_REGISTER_COMMAND(LTRIM, 4, "write", RoutingStrategy::kByFirstKey, HandleLTrim);
ASTRADB_REGISTER_COMMAND(LREM, 4, "write", RoutingStrategy::kByFirstKey, HandleLRem);
ASTRADB_REGISTER_COMMAND(LINSERT, 5, "write", RoutingStrategy::kByFirstKey, HandleLInsert);
ASTRADB_REGISTER_COMMAND(RPOPLPUSH, 3, "write", RoutingStrategy::kByFirstKey, HandleRPopLPush);
ASTRADB_REGISTER_COMMAND(BLPOP, -2, "write", RoutingStrategy::kByFirstKey, HandleBLPop);
ASTRADB_REGISTER_COMMAND(BRPOP, -2, "write", RoutingStrategy::kByFirstKey, HandleBRPop);
ASTRADB_REGISTER_COMMAND(BRPOPLPUSH, -3, "write", RoutingStrategy::kByFirstKey, HandleBRPopLPush);
ASTRADB_REGISTER_COMMAND(BLMOVE, -5, "write", RoutingStrategy::kByFirstKey, HandleBLMove);
ASTRADB_REGISTER_COMMAND(BLMPOP, -4, "write", RoutingStrategy::kByFirstKey, HandleBLMPop);

}  // namespace astra::commands