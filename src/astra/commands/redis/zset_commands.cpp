// ==============================================================================
// ZSet Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "zset_commands.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/inlined_vector.h>

#include "astra/protocol/resp/resp_builder.hpp"
#include "astra/server/worker_scheduler.hpp"
#include "blocking_manager.hpp"
#include "command_auto_register.hpp"

namespace astra::commands {

// ZADD key score member [score member ...]
CommandResult HandleZAdd(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() < 3 || command.ArgCount() % 2 != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZADD' command");
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

  for (size_t i = 1; i < command.ArgCount(); i += 2) {
    const auto& score_arg = command[i];
    const auto& member_arg = command[i + 1];

    if (!score_arg.IsBulkString() || !member_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of score or member argument");
    }

    double score;
    try {
      if (!absl::SimpleAtod(score_arg.AsString(), &score)) {
        return CommandResult(false, "ERR value is not a valid float");
      }
    } catch (...) {
      return CommandResult(false, "ERR value is not a valid float");
    }

    if (db->ZAdd(key, score, member_arg.AsString())) {
      ++count;
    }
  }

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// ZRANGE key start stop [WITHSCORES]
CommandResult HandleZRange(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 3 || command.ArgCount() > 4) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZRANGE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& start_arg = command[1];
  const auto& stop_arg = command[2];

  if (!key_arg.IsBulkString() || !start_arg.IsBulkString() ||
      !stop_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  int64_t start, stop;

  try {
    if (!absl::SimpleAtoi(start_arg.AsString(), &start)) {
      return CommandResult(false,
                           "ERR value is not an integer or out of range");
    }
    if (!absl::SimpleAtoi(stop_arg.AsString(), &stop)) {
      return CommandResult(false,
                           "ERR value is not an integer or out of range");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  bool with_scores = false;
  if (command.ArgCount() == 4) {
    const auto& option_arg = command[3];
    if (option_arg.IsBulkString()) {
      std::string opt = option_arg.AsString();
      std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
      if (opt == "WITHSCORES") {
        with_scores = true;
      } else {
        return CommandResult(false, "ERR syntax error");
      }
    }
  }

  auto results = db->ZRangeByRank(key, start, stop, false, with_scores);

  absl::InlinedVector<RespValue, 32> array;
  array.reserve(results.size());
  for (const auto& [member, score] : results) {
    if (with_scores) {
      array.emplace_back(RespValue(std::string(member)));
      array.emplace_back(RespValue(score));
    } else {
      array.emplace_back(RespValue(std::string(member)));
    }
  }

  return CommandResult(
      RespValue(std::vector<RespValue>(array.begin(), array.end())));
}

// ZREM key member [member ...]
CommandResult HandleZRem(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZREM' command");
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
  std::vector<std::string> members;
  members.reserve(command.ArgCount() - 1);

  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of member argument");
    }
    members.push_back(arg.AsString());
  }

  size_t count = 0;
  for (const auto& member : members) {
    if (db->ZRem(key, member)) {
      ++count;
    }
  }
  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// ZSCORE key member
CommandResult HandleZScore(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZSCORE' command");
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

  auto score = db->ZScore(key, member);
  if (score.has_value()) {
    return CommandResult(RespValue(*score));
  } else {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }
}

// ZCARD key
CommandResult HandleZCard(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZCARD' command");
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
  size_t count = db->ZCard(key);
  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// ZCOUNT key min max
CommandResult HandleZCount(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZCOUNT' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& min_arg = command[1];
  const auto& max_arg = command[2];

  if (!key_arg.IsBulkString() || !min_arg.IsBulkString() ||
      !max_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  double min, max;

  try {
    if (!absl::SimpleAtod(min_arg.AsString(), &min)) {
      return CommandResult(false, "ERR value is not a valid float");
    }
    if (!absl::SimpleAtod(max_arg.AsString(), &max)) {
      return CommandResult(false, "ERR value is not a valid float");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not a valid float");
  }

  uint64_t count = db->ZCount(key, min, max);
  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// ZINCRBY key increment member
CommandResult HandleZIncrBy(const astra::protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZINCRBY' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& incr_arg = command[1];
  const auto& member_arg = command[2];

  if (!key_arg.IsBulkString() || !incr_arg.IsBulkString() ||
      !member_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string member = member_arg.AsString();
  double increment;

  try {
    if (!absl::SimpleAtod(incr_arg.AsString(), &increment)) {
      return CommandResult(false, "ERR value is not a valid float");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not a valid float");
  }

  double new_score = db->ZIncrBy(key, increment, member);

  // Log to AOF
  std::string incr_str = incr_arg.AsString();
  std::array<absl::string_view, 3> aof_args = {key, incr_str, member};
  context->LogToAof("ZINCRBY", aof_args);

  return CommandResult(RespValue(new_score));
}

// ZRANK key member
CommandResult HandleZRank(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZRANK' command");
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

  auto rank = db->ZRank(key, member, false);
  if (!rank.has_value()) {
    return CommandResult(RespValue());  // nil
  }
  return CommandResult(RespValue(static_cast<int64_t>(*rank)));
}

// ZREVRANK key member
CommandResult HandleZRevRank(const astra::protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZREVRANK' command");
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

  auto rank = db->ZRank(key, member, true);  // reverse = true
  if (!rank.has_value()) {
    return CommandResult(RespValue());  // nil
  }
  return CommandResult(RespValue(static_cast<int64_t>(*rank)));
}

// BZPOPMIN key [key ...] timeout
// Note: This is a simplified implementation without real blocking.
// TODO: Implement real blocking with wait queues and timeout management
// TODO: Implement client notification when data becomes available
// TODO: Track blocked clients and wake them up when data is pushed
CommandResult HandleBZPopMin(const astra::protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'BZPOPMIN' command");
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

  // Try to pop from each key
  for (size_t i = 0; i < command.ArgCount() - 1; ++i) {
    const auto& key_arg = command[i];

    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }

    std::string key = key_arg.AsString();

    // Pop the member with minimum score
    auto result = db->ZPopMin(key);
    if (result.has_value()) {
      // Return [key, member, score]
      absl::InlinedVector<RespValue, 32> resp;
      resp.push_back(RespValue(key));
      resp.push_back(RespValue(result->first));
      resp.push_back(RespValue(result->second));
      return CommandResult(
          RespValue(std::vector<RespValue>(resp.begin(), resp.end())));
    }
  }

  // All sorted sets are empty, implement blocking
  if (timeout > 0) {
    // Get blocking manager
    auto* blocking_manager = context->GetBlockingManager();
    if (!blocking_manager) {
      // Blocking manager not available, return nil
      return CommandResult(RespValue(RespType::kNullBulkString));
    }

    // Get connection ID
    uint64_t client_id = context->GetConnectionId();

    // Collect all keys for blocking
    std::vector<std::string> keys;
    for (size_t i = 0; i < command.ArgCount() - 1; ++i) {
      const auto& key_arg = command[i];
      if (key_arg.IsBulkString()) {
        keys.push_back(key_arg.AsString());
      }
    }

    if (!keys.empty()) {
      // Create blocked client with callback
      BlockedClient blocked_client;
      blocked_client.client_id = client_id;
      blocked_client.key = keys[0];  // Use first key for tracking
      blocked_client.command = command;
      blocked_client.timeout_seconds = timeout;
      blocked_client.start_time = std::chrono::steady_clock::now();
      blocked_client.connection =
          context->GetConnection();  // Save connection pointer

      // Set callback to execute pop when woken up
      blocked_client.callback =
          [db, keys](const RespValue& notification) -> RespValue {
        // When woken up, try to pop from each key
        for (const auto& key : keys) {
          // Pop the member with minimum score
          auto result = db->ZPopMin(key);
          if (result.has_value()) {
            // Return [key, member, score]
            absl::InlinedVector<RespValue, 32> resp;
            resp.push_back(RespValue(key));
            resp.push_back(RespValue(result->first));
            resp.push_back(RespValue(result->second));
            return RespValue(std::vector<RespValue>(resp.begin(), resp.end()));
          }
        }

        // All sorted sets still empty
        return RespValue(RespType::kNullBulkString);
      };

      // Add to blocking queue (use first key)
      blocking_manager->AddBlockedClient(keys[0], std::move(blocked_client));

      // Return blocking result (response will be sent later)
      return CommandResult::Blocking();
    }
  }

  // Non-blocking mode, return nil
  return CommandResult(RespValue(RespType::kNullBulkString));
}

// BZPOPMAX key [key ...] timeout
// Note: This is a simplified implementation without real blocking.
// TODO: Implement real blocking with wait queues and timeout management
// TODO: Implement client notification when data becomes available
// TODO: Track blocked clients and wake them up when data is pushed
CommandResult HandleBZPopMax(const astra::protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'BZPOPMAX' command");
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

  // Try to pop from each key
  for (size_t i = 0; i < command.ArgCount() - 1; ++i) {
    const auto& key_arg = command[i];

    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }

    std::string key = key_arg.AsString();

    // Pop the member with maximum score
    auto result = db->ZPopMax(key);
    if (result.has_value()) {
      // Return [key, member, score]
      std::vector<RespValue> resp;
      resp.push_back(RespValue(key));
      resp.push_back(RespValue(result->first));
      resp.push_back(RespValue(result->second));
      return CommandResult(
          RespValue(std::vector<RespValue>(resp.begin(), resp.end())));
    }
  }

  // All sorted sets are empty, implement blocking
  if (timeout > 0) {
    // Get blocking manager
    auto* blocking_manager = context->GetBlockingManager();
    if (!blocking_manager) {
      // Blocking manager not available, return nil
      return CommandResult(RespValue(RespType::kNullBulkString));
    }

    // Get connection ID
    uint64_t client_id = context->GetConnectionId();

    // Collect all keys for blocking
    std::vector<std::string> keys;
    for (size_t i = 0; i < command.ArgCount() - 1; ++i) {
      const auto& key_arg = command[i];
      if (key_arg.IsBulkString()) {
        keys.push_back(key_arg.AsString());
      }
    }

    if (!keys.empty()) {
      // Create blocked client with callback
      BlockedClient blocked_client;
      blocked_client.client_id = client_id;
      blocked_client.key = keys[0];  // Use first key for tracking
      blocked_client.command = command;
      blocked_client.timeout_seconds = timeout;
      blocked_client.start_time = std::chrono::steady_clock::now();
      blocked_client.connection =
          context->GetConnection();  // Save connection pointer

      // Set callback to execute pop when woken up
      blocked_client.callback =
          [db, keys](const RespValue& notification) -> RespValue {
        // When woken up, try to pop from each key
        for (const auto& key : keys) {
          // Pop the member with maximum score
          auto result = db->ZPopMax(key);
          if (result.has_value()) {
            // Return [key, member, score]
            absl::InlinedVector<RespValue, 32> resp;
            resp.push_back(RespValue(key));
            resp.push_back(RespValue(result->first));
            resp.push_back(RespValue(result->second));
            return RespValue(std::vector<RespValue>(resp.begin(), resp.end()));
          }
        }

        // All sorted sets still empty
        return RespValue(RespType::kNullBulkString);
      };

      // Add to blocking queue (use first key)
      blocking_manager->AddBlockedClient(keys[0], std::move(blocked_client));

      // Return blocking result (response will be sent later)
      return CommandResult::Blocking();
    }
  }

  // Non-blocking mode, return nil
  return CommandResult(RespValue(RespType::kNullBulkString));
}

// BZMPOP numkeys key [key ...] timeout COUNT count [MIN|MAX]
// Note: This is a simplified implementation without real blocking.
// TODO: Implement real blocking with wait queues and timeout management
// TODO: Implement client notification when data becomes available
// TODO: Track blocked clients and wake them up when data is pushed
CommandResult HandleBZMPop(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 4) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'BZMPOP' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  // Parse numkeys (first argument)
  const auto& numkeys_arg = command[0];

  int64_t numkeys = 0;
  if (numkeys_arg.IsInteger()) {
    numkeys = numkeys_arg.AsInteger();
  } else if (numkeys_arg.IsBulkString()) {
    try {
      numkeys = std::stoll(numkeys_arg.AsString());
    } catch (...) {
      return CommandResult(false, "ERR numkeys must be an integer");
    }
  } else {
    return CommandResult(false, "ERR numkeys must be an integer");
  }

  if (numkeys < 1) {
    return CommandResult(false, "ERR numkeys must be positive");
  }

  // Calculate timeout index (numkeys + 1 for timeout)
  size_t timeout_idx = 1 + numkeys;
  if (timeout_idx >= command.ArgCount()) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'BZMPOP' command");
  }

  const auto& timeout_arg = command[timeout_idx];
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

  // Parse COUNT and direction (optional)
  int64_t count = 1;
  bool pop_max = false;  // default is MIN

  for (size_t i = timeout_idx + 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) continue;

    std::string arg_str = arg.AsString();
    std::string arg_lower;
    for (char c : arg_str) {
      arg_lower += std::tolower(static_cast<unsigned char>(c));
    }
    if (arg_lower == "min") {
      pop_max = false;
    } else if (arg_lower == "max") {
      pop_max = true;
    } else if (arg_lower == "count") {
      if (i + 1 < command.ArgCount()) {
        const auto& count_arg = command[i + 1];
        if (count_arg.IsInteger()) {
          count = count_arg.AsInteger();
        } else if (count_arg.IsBulkString()) {
          try {
            count = std::stoll(count_arg.AsString());
          } catch (...) {
            return CommandResult(false,
                                 "ERR count is not an integer or out of range");
          }
        }
        ++i;  // skip the count value
      }
    }
  }

  if (count < 1) {
    return CommandResult(false, "ERR count must be positive");
  }

  // Try to pop from each key
  for (size_t i = 1; i <= static_cast<size_t>(numkeys); ++i) {
    const auto& key_arg = command[i];

    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }

    std::string key = key_arg.AsString();

    // Pop multiple members
    std::vector<std::pair<std::string, double>> results;
    bool popped = false;

    for (int64_t j = 0; j < count; ++j) {
      if (pop_max) {
        auto result = db->ZPopMax(key);
        if (result.has_value()) {
          results.push_back(*result);
          popped = true;
        } else {
          break;
        }
      } else {
        auto result = db->ZPopMin(key);
        if (result.has_value()) {
          results.push_back(*result);
          popped = true;
        } else {
          break;
        }
      }
    }

    if (popped) {
      // Return [key, [[member1, score1], [member2, score2], ...]]
      std::vector<RespValue> outer_resp;
      outer_resp.push_back(RespValue(key));

      std::vector<RespValue> inner_resp;
      for (const auto& [member, score] : results) {
        std::vector<RespValue> member_score;
        member_score.push_back(RespValue(member));
        member_score.push_back(RespValue(score));
        inner_resp.push_back(RespValue(std::move(member_score)));
      }
      outer_resp.push_back(RespValue(std::move(inner_resp)));

      return CommandResult(RespValue(std::move(outer_resp)));
    }
  }

  // All sorted sets are empty, implement blocking
  if (timeout > 0) {
    // Get blocking manager
    auto* blocking_manager = context->GetBlockingManager();
    if (!blocking_manager) {
      // Blocking manager not available, return nil
      return CommandResult(RespValue(RespType::kNullBulkString));
    }

    // Get connection ID
    uint64_t client_id = context->GetConnectionId();

    // Collect all keys for blocking
    std::vector<std::string> keys;
    for (size_t i = 1; i <= static_cast<size_t>(numkeys); ++i) {
      const auto& key_arg = command[i];
      if (key_arg.IsBulkString()) {
        keys.push_back(key_arg.AsString());
      }
    }

    if (!keys.empty()) {
      // Create blocked client with callback
      BlockedClient blocked_client;
      blocked_client.client_id = client_id;
      blocked_client.key = keys[0];  // Use first key for tracking
      blocked_client.command = command;
      blocked_client.timeout_seconds = timeout;
      blocked_client.start_time = std::chrono::steady_clock::now();
      blocked_client.connection =
          context->GetConnection();  // Save connection pointer

      // Set callback to execute pop when woken up
      blocked_client.callback =
          [db, keys, count,
           pop_max](const RespValue& notification) -> RespValue {
        // When woken up, try to pop from each key
        for (const auto& key : keys) {
          // Pop multiple members
          std::vector<std::pair<std::string, double>> results;
          bool popped = false;

          for (int64_t j = 0; j < count; ++j) {
            if (pop_max) {
              auto result = db->ZPopMax(key);
              if (result.has_value()) {
                results.push_back(*result);
                popped = true;
              } else {
                break;
              }
            } else {
              auto result = db->ZPopMin(key);
              if (result.has_value()) {
                results.push_back(*result);
                popped = true;
              } else {
                break;
              }
            }
          }

          if (popped) {
            // Return [key, [[member1, score1], [member2, score2], ...]]
            std::vector<RespValue> outer_resp;
            outer_resp.push_back(RespValue(key));

            std::vector<RespValue> inner_resp;
            for (const auto& [member, score] : results) {
              std::vector<RespValue> member_score;
              member_score.push_back(RespValue(member));
              member_score.push_back(RespValue(score));
              inner_resp.push_back(RespValue(std::move(member_score)));
            }
            outer_resp.push_back(RespValue(std::move(inner_resp)));

            return RespValue(
                std::vector<RespValue>(outer_resp.begin(), outer_resp.end()));
          }
        }

        // All sorted sets still empty
        return RespValue(RespType::kNullBulkString);
      };

      // Add to blocking queue (use first key)
      blocking_manager->AddBlockedClient(keys[0], std::move(blocked_client));

      // Return blocking result (response will be sent later)
      return CommandResult::Blocking();
    }
  }

  // Non-blocking mode, return nil
  return CommandResult(RespValue(RespType::kNullBulkString));
}

// ZREVRANGE key start stop [WITHSCORES]
CommandResult HandleZRevRange(const astra::protocol::Command& command,
                              CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZREVRANGE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& start_arg = command[1];
  const auto& stop_arg = command[2];

  if (!key_arg.IsBulkString() || !start_arg.IsBulkString() ||
      !stop_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();

  int64_t start = 0;
  int64_t stop = -1;

  try {
    start = std::stoll(start_arg.AsString());
    stop = std::stoll(stop_arg.AsString());
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  bool with_scores = false;
  if (command.ArgCount() > 3) {
    const auto& scores_arg = command[3];
    if (scores_arg.IsBulkString()) {
      std::string scores_str = scores_arg.AsString();
      std::string scores_lower;
      for (char c : scores_str) {
        scores_lower += std::tolower(static_cast<unsigned char>(c));
      }
      if (scores_lower == "withscores") {
        with_scores = true;
      }
    }
  }

  auto results =
      db->ZRange(key, start, stop, true, with_scores);  // reverse = true

  if (with_scores) {
    std::vector<RespValue> resp;
    for (const auto& [member, score] : results) {
      resp.push_back(RespValue(member));
      resp.push_back(RespValue(score));
    }
    return CommandResult(
        RespValue(std::vector<RespValue>(resp.begin(), resp.end())));
  } else {
    absl::InlinedVector<RespValue, 32> resp;
    for (const auto& [member, score] : results) {
      resp.push_back(RespValue(member));
    }
    return CommandResult(
        RespValue(std::vector<RespValue>(resp.begin(), resp.end())));
  }
}

// ZRANGEBYSCORE key min max [WITHSCORES] [LIMIT offset count]
CommandResult HandleZRangeByScore(const astra::protocol::Command& command,
                                  CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZRANGEBYSCORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& min_arg = command[1];
  const auto& max_arg = command[2];

  if (!key_arg.IsBulkString() || !min_arg.IsBulkString() ||
      !max_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string min_str = min_arg.AsString();
  std::string max_str = max_arg.AsString();

  bool with_scores = false;
  int64_t offset = 0;
  int64_t count = -1;
  bool has_limit = false;

  for (size_t i = 3; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) continue;

    std::string arg_str = arg.AsString();
    std::string arg_lower;
    for (char c : arg_str) {
      arg_lower += std::tolower(static_cast<unsigned char>(c));
    }
    if (arg_lower == "withscores") {
      with_scores = true;
    } else if (arg_lower == "limit") {
      has_limit = true;
      if (i + 2 < command.ArgCount()) {
        try {
          offset = std::stoll(command[i + 1].AsString());
          count = std::stoll(command[i + 2].AsString());
          i += 2;
        } catch (...) {
          return CommandResult(false, "ERR syntax error");
        }
      }
    }
  }

  auto results = db->ZRangeByScore(key, min_str, max_str, false, with_scores,
                                   has_limit, offset, count);

  if (with_scores) {
    absl::InlinedVector<RespValue, 32> resp;
    for (const auto& [member, score] : results) {
      resp.push_back(RespValue(member));
      resp.push_back(RespValue(score));
    }
    return CommandResult(
        RespValue(std::vector<RespValue>(resp.begin(), resp.end())));
  } else {
    absl::InlinedVector<RespValue, 32> resp;
    for (const auto& [member, score] : results) {
      resp.push_back(RespValue(member));
    }
    return CommandResult(
        RespValue(std::vector<RespValue>(resp.begin(), resp.end())));
  }
}

// ZREVRANGEBYSCORE key max min [WITHSCORES] [LIMIT offset count]
CommandResult HandleZRevRangeByScore(const astra::protocol::Command& command,
                                     CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZREVRANGEBYSCORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& max_arg = command[1];
  const auto& min_arg = command[2];

  if (!key_arg.IsBulkString() || !max_arg.IsBulkString() ||
      !min_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string min_str = min_arg.AsString();
  std::string max_str = max_arg.AsString();

  bool with_scores = false;
  int64_t offset = 0;
  int64_t count = -1;
  bool has_limit = false;

  for (size_t i = 3; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) continue;

    std::string arg_str = arg.AsString();
    std::string arg_lower;
    for (char c : arg_str) {
      arg_lower += std::tolower(static_cast<unsigned char>(c));
    }
    if (arg_lower == "withscores") {
      with_scores = true;
    } else if (arg_lower == "limit") {
      has_limit = true;
      if (i + 2 < command.ArgCount()) {
        try {
          offset = std::stoll(command[i + 1].AsString());
          count = std::stoll(command[i + 2].AsString());
          i += 2;
        } catch (...) {
          return CommandResult(false, "ERR syntax error");
        }
      }
    }
  }

  auto results = db->ZRangeByScore(key, min_str, max_str, true, with_scores,
                                   has_limit, offset, count);

  if (with_scores) {
    absl::InlinedVector<RespValue, 32> resp;
    for (const auto& [member, score] : results) {
      resp.push_back(RespValue(member));
      resp.push_back(RespValue(score));
    }
    return CommandResult(
        RespValue(std::vector<RespValue>(resp.begin(), resp.end())));
  } else {
    absl::InlinedVector<RespValue, 32> resp;
    for (const auto& [member, score] : results) {
      resp.push_back(RespValue(member));
    }
    return CommandResult(
        RespValue(std::vector<RespValue>(resp.begin(), resp.end())));
  }
}

// ZPOPMIN key [count]
CommandResult HandleZPopMin(const astra::protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZPOPMIN' command");
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

  int64_t count = 1;
  if (command.ArgCount() > 1) {
    const auto& count_arg = command[1];
    if (count_arg.IsInteger()) {
      count = count_arg.AsInteger();
    } else if (count_arg.IsBulkString()) {
      try {
        count = std::stoll(count_arg.AsString());
      } catch (...) {
        return CommandResult(false,
                             "ERR count is not an integer or out of range");
      }
    }
  }

  if (count < 1) {
    return CommandResult(false, "ERR count must be positive");
  }

  std::vector<std::pair<std::string, double>> results;
  for (int64_t i = 0; i < count; ++i) {
    auto result = db->ZPopMin(key);
    if (result.has_value()) {
      results.push_back(*result);
    } else {
      break;
    }
  }

  if (results.empty()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  absl::InlinedVector<RespValue, 32> resp;
  for (const auto& [member, score] : results) {
    std::vector<RespValue> member_score;
    member_score.push_back(RespValue(member));
    member_score.push_back(RespValue(score));
    resp.push_back(RespValue(std::move(member_score)));
  }

  return CommandResult(
      RespValue(std::vector<RespValue>(resp.begin(), resp.end())));
}

// ZPOPMAX key [count]
CommandResult HandleZPopMax(const astra::protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZPOPMAX' command");
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

  int64_t count = 1;
  if (command.ArgCount() > 1) {
    const auto& count_arg = command[1];
    if (count_arg.IsInteger()) {
      count = count_arg.AsInteger();
    } else if (count_arg.IsBulkString()) {
      try {
        count = std::stoll(count_arg.AsString());
      } catch (...) {
        return CommandResult(false,
                             "ERR count is not an integer or out of range");
      }
    }
  }

  if (count < 1) {
    return CommandResult(false, "ERR count must be positive");
  }

  std::vector<std::pair<std::string, double>> results;
  for (int64_t i = 0; i < count; ++i) {
    auto result = db->ZPopMax(key);
    if (result.has_value()) {
      results.push_back(*result);
    } else {
      break;
    }
  }

  if (results.empty()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  absl::InlinedVector<RespValue, 32> resp;
  for (const auto& [member, score] : results) {
    std::vector<RespValue> member_score;
    member_score.push_back(RespValue(member));
    member_score.push_back(RespValue(score));
    resp.push_back(RespValue(std::move(member_score)));
  }

  return CommandResult(
      RespValue(std::vector<RespValue>(resp.begin(), resp.end())));
}

// ZUNIONSTORE destination numkeys key [key ...] [WEIGHTS weight [weight ...]]
// [AGGREGATE SUM|MIN|MAX]
CommandResult HandleZUnionStore(const astra::protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZUNIONSTORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& dest_arg = command[0];
  if (!dest_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of destination argument");
  }

  const auto& numkeys_arg = command[1];
  if (!numkeys_arg.IsInteger()) {
    return CommandResult(false, "ERR numkeys must be an integer");
  }

  size_t numkeys = static_cast<size_t>(numkeys_arg.AsInteger());
  if (numkeys == 0 || command.ArgCount() < 2 + numkeys) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZUNIONSTORE' command");
  }

  std::string destination = dest_arg.AsString();
  std::vector<std::string> keys;

  for (size_t i = 0; i < numkeys; ++i) {
    const auto& key_arg = command[2 + i];
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(key_arg.AsString());
  }

  // Parse optional WEIGHTS and AGGREGATE
  std::vector<double> weights;
  std::string aggregate = "SUM";

  size_t pos = 2 + numkeys;
  while (pos < command.ArgCount()) {
    const auto& arg = command[pos];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of argument");
    }

    std::string arg_str = arg.AsString();
    if (arg_str == "WEIGHTS") {
      ++pos;
      for (size_t i = 0; i < numkeys && pos < command.ArgCount(); ++i) {
        const auto& weight_arg = command[pos];
        if (!weight_arg.IsBulkString()) {
          return CommandResult(false, "ERR wrong type of weight argument");
        }
        try {
          weights.push_back(std::stod(weight_arg.AsString()));
        } catch (...) {
          return CommandResult(false, "ERR invalid weight value");
        }
        ++pos;
      }
    } else if (arg_str == "AGGREGATE") {
      ++pos;
      if (pos >= command.ArgCount()) {
        return CommandResult(false,
                             "ERR wrong number of arguments for 'AGGREGATE'");
      }
      const auto& agg_arg = command[pos];
      if (!agg_arg.IsBulkString()) {
        return CommandResult(false, "ERR wrong type of aggregate argument");
      }
      aggregate = agg_arg.AsString();
      if (aggregate != "SUM" && aggregate != "MIN" && aggregate != "MAX") {
        return CommandResult(false, "ERR invalid aggregate function");
      }
      ++pos;
    } else {
      return CommandResult(false, "ERR unknown argument");
    }
  }

  size_t count =
      db->ZUnionStore(destination, numkeys, keys, weights, aggregate);

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// ZINTERSTORE destination numkeys key [key ...] [WEIGHTS weight [weight ...]]
// [AGGREGATE SUM|MIN|MAX]
CommandResult HandleZInterStore(const astra::protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZINTERSTORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& dest_arg = command[0];
  if (!dest_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of destination argument");
  }

  const auto& numkeys_arg = command[1];
  if (!numkeys_arg.IsInteger()) {
    return CommandResult(false, "ERR numkeys must be an integer");
  }

  size_t numkeys = static_cast<size_t>(numkeys_arg.AsInteger());
  if (numkeys == 0 || command.ArgCount() < 2 + numkeys) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZINTERSTORE' command");
  }

  std::string destination = dest_arg.AsString();
  std::vector<std::string> keys;

  for (size_t i = 0; i < numkeys; ++i) {
    const auto& key_arg = command[2 + i];
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(key_arg.AsString());
  }

  // Parse optional WEIGHTS and AGGREGATE
  std::vector<double> weights;
  std::string aggregate = "SUM";

  size_t pos = 2 + numkeys;
  while (pos < command.ArgCount()) {
    const auto& arg = command[pos];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of argument");
    }

    std::string arg_str = arg.AsString();
    if (arg_str == "WEIGHTS") {
      ++pos;
      for (size_t i = 0; i < numkeys && pos < command.ArgCount(); ++i) {
        const auto& weight_arg = command[pos];
        if (!weight_arg.IsBulkString()) {
          return CommandResult(false, "ERR wrong type of weight argument");
        }
        try {
          weights.push_back(std::stod(weight_arg.AsString()));
        } catch (...) {
          return CommandResult(false, "ERR invalid weight value");
        }
        ++pos;
      }
    } else if (arg_str == "AGGREGATE") {
      ++pos;
      if (pos >= command.ArgCount()) {
        return CommandResult(false,
                             "ERR wrong number of arguments for 'AGGREGATE'");
      }
      const auto& agg_arg = command[pos];
      if (!agg_arg.IsBulkString()) {
        return CommandResult(false, "ERR wrong type of aggregate argument");
      }
      aggregate = agg_arg.AsString();
      if (aggregate != "SUM" && aggregate != "MIN" && aggregate != "MAX") {
        return CommandResult(false, "ERR invalid aggregate function");
      }
      ++pos;
    } else {
      return CommandResult(false, "ERR unknown argument");
    }
  }

  size_t count =
      db->ZInterStore(destination, numkeys, keys, weights, aggregate);

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// ZDIFF numkeys key [key ...] [WITHSCORES]
CommandResult HandleZDiff(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZDIFF' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& numkeys_arg = command[0];
  if (!numkeys_arg.IsInteger()) {
    return CommandResult(false, "ERR numkeys must be an integer");
  }

  size_t numkeys = static_cast<size_t>(numkeys_arg.AsInteger());
  if (numkeys == 0 || command.ArgCount() < 1 + numkeys) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZDIFF' command");
  }

  std::vector<std::string> keys;
  for (size_t i = 0; i < numkeys; ++i) {
    const auto& key_arg = command[1 + i];
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(key_arg.AsString());
  }

  // Check for WITHSCORES option
  bool with_scores = false;
  if (command.ArgCount() > 1 + numkeys) {
    const auto& last_arg = command[command.ArgCount() - 1];
    if (last_arg.IsBulkString() && last_arg.AsString() == "WITHSCORES") {
      with_scores = true;
    }
  }

  // Try to use WorkerScheduler for cross-shard query (NO SHARING architecture)
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    server::Worker* current_worker = context->GetWorker();

    // Collect all members with scores from all zsets across all workers
    std::vector<std::future<std::vector<
        std::pair<std::string, absl::flat_hash_map<std::string, double>>>>>
        futures;
    futures.reserve(all_workers.size());

    for (size_t worker_id = 0; worker_id < all_workers.size(); ++worker_id) {
      auto promise = std::make_shared<std::promise<std::vector<
          std::pair<std::string, absl::flat_hash_map<std::string, double>>>>>();
      auto future = promise->get_future();
      futures.push_back(std::move(future));

      server::Worker* target_worker = all_workers[worker_id];
      std::vector<std::string> keys_copy = keys;

      // Check if this is the current worker - execute directly to avoid
      // deadlock
      if (target_worker == current_worker) {
        // Execute directly in current thread
        std::vector<
            std::pair<std::string, absl::flat_hash_map<std::string, double>>>
            worker_zsets;
        Database* db = &target_worker->GetDataShard().GetDatabase();

        for (const auto& key : keys_copy) {
          absl::flat_hash_map<std::string, double> zset_data;
          auto members_with_scores = db->ZRangeByRank(key, 0, -1, true, false);
          for (const auto& [member, score] : members_with_scores) {
            zset_data[member] = score;
          }
          worker_zsets.push_back({key, std::move(zset_data)});
        }

        promise->set_value(worker_zsets);
      } else {
        // Execute on other worker via queue
        all_workers[worker_id]->AddTask([keys_copy, target_worker, promise]() {
          try {
            std::vector<std::pair<std::string,
                                  absl::flat_hash_map<std::string, double>>>
                worker_zsets;
            Database* db = &target_worker->GetDataShard().GetDatabase();

            for (const auto& key : keys_copy) {
              absl::flat_hash_map<std::string, double> zset_data;
              auto members_with_scores =
                  db->ZRangeByRank(key, 0, -1, true, false);
              for (const auto& [member, score] : members_with_scores) {
                zset_data[member] = score;
              }
              worker_zsets.push_back({key, std::move(zset_data)});
            }

            promise->set_value(worker_zsets);
          } catch (...) {
            promise->set_exception(std::current_exception());
          }
        });

        // Notify worker to process task immediately
        all_workers[worker_id]->NotifyTaskProcessing();
      }
    }

    // Aggregate results from all workers
    absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, double>>
        all_zsets;
    for (auto& future : futures) {
      auto worker_zsets = future.get();
      for (const auto& [key, zset_data] : worker_zsets) {
        all_zsets[key].insert(zset_data.begin(), zset_data.end());
      }
    }

    // Compute difference: first zset minus all other zsets
    if (all_zsets.find(keys[0]) == all_zsets.end()) {
      std::vector<std::pair<std::string, double>> empty_diff;
      if (with_scores) {
        std::vector<RespValue> resp;
        return CommandResult(RespValue(std::move(resp)));
      } else {
        std::vector<RespValue> resp;
        return CommandResult(RespValue(std::move(resp)));
      }
    }

    const auto& first_zset = all_zsets[keys[0]];
    std::vector<std::pair<std::string, double>> diff_result;

    for (const auto& [member, score] : first_zset) {
      bool in_others = false;
      for (size_t i = 1; i < keys.size(); ++i) {
        const auto& zset = all_zsets[keys[i]];
        if (zset.find(member) != zset.end()) {
          in_others = true;
          break;
        }
      }
      if (!in_others) {
        diff_result.emplace_back(member, score);
      }
    }

    // Sort by score
    std::sort(diff_result.begin(), diff_result.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    if (with_scores) {
      std::vector<RespValue> resp;
      for (const auto& [member, score] : diff_result) {
        resp.push_back(RespValue(member));
        resp.push_back(RespValue(score));
      }
      return CommandResult(RespValue(std::move(resp)));
    } else {
      std::vector<RespValue> resp;
      for (const auto& [member, _] : diff_result) {
        resp.push_back(RespValue(member));
      }
      return CommandResult(RespValue(std::move(resp)));
    }
  }

  // Fallback: single worker mode
  auto members = db->ZDiff(numkeys, keys);

  if (with_scores) {
    std::vector<RespValue> resp;
    for (const auto& [member, score] : members) {
      resp.push_back(RespValue(member));
      resp.push_back(RespValue(score));
    }
    return CommandResult(RespValue(std::move(resp)));
  } else {
    std::vector<RespValue> resp;
    for (const auto& [member, _] : members) {
      resp.push_back(RespValue(member));
    }
    return CommandResult(RespValue(std::move(resp)));
  }
}

// ZDIFFSTORE destination numkeys key [key ...]
CommandResult HandleZDiffStore(const astra::protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZDIFFSTORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& dest_arg = command[0];
  if (!dest_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of destination argument");
  }

  const auto& numkeys_arg = command[1];
  if (!numkeys_arg.IsInteger()) {
    return CommandResult(false, "ERR numkeys must be an integer");
  }

  size_t numkeys = static_cast<size_t>(numkeys_arg.AsInteger());
  if (numkeys == 0 || command.ArgCount() < 2 + numkeys) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZDIFFSTORE' command");
  }

  std::string destination = dest_arg.AsString();
  std::vector<std::string> keys;

  for (size_t i = 0; i < numkeys; ++i) {
    const auto& key_arg = command[2 + i];
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(key_arg.AsString());
  }

  size_t count = db->ZDiffStore(destination, numkeys, keys);

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// Auto-register all zset commands
ASTRADB_REGISTER_COMMAND(ZADD, -4, "write", RoutingStrategy::kByFirstKey,
                         HandleZAdd);
ASTRADB_REGISTER_COMMAND(ZRANGE, -4, "readonly", RoutingStrategy::kByFirstKey,
                         HandleZRange);
ASTRADB_REGISTER_COMMAND(ZREM, -3, "write", RoutingStrategy::kByFirstKey,
                         HandleZRem);
ASTRADB_REGISTER_COMMAND(ZSCORE, 3, "readonly", RoutingStrategy::kByFirstKey,
                         HandleZScore);
ASTRADB_REGISTER_COMMAND(ZCARD, 2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleZCard);
ASTRADB_REGISTER_COMMAND(ZCOUNT, 4, "readonly", RoutingStrategy::kByFirstKey,
                         HandleZCount);
ASTRADB_REGISTER_COMMAND(ZINCRBY, 4, "write", RoutingStrategy::kByFirstKey,
                         HandleZIncrBy);
ASTRADB_REGISTER_COMMAND(ZRANK, 3, "readonly", RoutingStrategy::kByFirstKey,
                         HandleZRank);
ASTRADB_REGISTER_COMMAND(ZREVRANK, 3, "readonly", RoutingStrategy::kByFirstKey,
                         HandleZRevRank);
ASTRADB_REGISTER_COMMAND(ZREVRANGE, -4, "readonly",
                         RoutingStrategy::kByFirstKey, HandleZRevRange);
ASTRADB_REGISTER_COMMAND(ZRANGEBYSCORE, -4, "readonly",
                         RoutingStrategy::kByFirstKey, HandleZRangeByScore);
ASTRADB_REGISTER_COMMAND(ZREVRANGEBYSCORE, -4, "readonly",
                         RoutingStrategy::kByFirstKey, HandleZRevRangeByScore);
ASTRADB_REGISTER_COMMAND(ZPOPMIN, -1, "write", RoutingStrategy::kByFirstKey,
                         HandleZPopMin);
ASTRADB_REGISTER_COMMAND(ZPOPMAX, -1, "write", RoutingStrategy::kByFirstKey,
                         HandleZPopMax);
ASTRADB_REGISTER_COMMAND(ZUNIONSTORE, -4, "write", RoutingStrategy::kByFirstKey,
                         HandleZUnionStore);
ASTRADB_REGISTER_COMMAND(ZINTERSTORE, -4, "write", RoutingStrategy::kByFirstKey,
                         HandleZInterStore);
ASTRADB_REGISTER_COMMAND(ZDIFF, -2, "readonly", RoutingStrategy::kNone,
                         HandleZDiff);
ASTRADB_REGISTER_COMMAND(ZDIFFSTORE, -3, "write", RoutingStrategy::kByFirstKey,
                         HandleZDiffStore);
ASTRADB_REGISTER_COMMAND(BZPOPMIN, -2, "write", RoutingStrategy::kByFirstKey,
                         HandleBZPopMin);
ASTRADB_REGISTER_COMMAND(BZPOPMAX, -2, "write", RoutingStrategy::kByFirstKey,
                         HandleBZPopMax);
ASTRADB_REGISTER_COMMAND(BZMPOP, -4, "write", RoutingStrategy::kByFirstKey,
                         HandleBZMPop);

// ZINTER numkeys key [key ...] [WEIGHTS weight [weight ...]] [AGGREGATE
// SUM|MIN|MAX] - Compute the intersection of sorted sets
CommandResult HandleZInter(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZINTER' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& numkeys_arg = command[0];
  if (!numkeys_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of numkeys argument");
  }

  int64_t numkeys;
  if (!absl::SimpleAtoi(numkeys_arg.AsString(), &numkeys) || numkeys <= 0) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  if (command.ArgCount() < static_cast<size_t>(numkeys + 1)) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZINTER' command");
  }

  std::vector<std::string> keys;
  for (int64_t i = 0; i < numkeys; ++i) {
    const auto& key_arg = command[static_cast<size_t>(i) + 1];
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(key_arg.AsString());
  }

  // Parse options
  bool with_scores = false;
  std::vector<double> weights(numkeys, 1.0);
  std::string aggregate = "SUM";

  size_t pos = static_cast<size_t>(numkeys) + 1;
  while (pos < command.ArgCount()) {
    const auto& opt_arg = command[pos];
    if (!opt_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of option argument");
    }

    std::string opt = opt_arg.AsString();
    if (opt == "WITHSCORES") {
      with_scores = true;
      ++pos;
    } else if (opt == "WEIGHTS" && pos + numkeys < command.ArgCount()) {
      for (int64_t i = 0; i < numkeys; ++i) {
        ++pos;
        double weight;
        if (!absl::SimpleAtod(command[pos].AsString(), &weight)) {
          return CommandResult(false, "ERR weight is not a valid float");
        }
        weights[static_cast<size_t>(i)] = weight;
      }
      ++pos;
    } else if (opt == "AGGREGATE" && pos + 1 < command.ArgCount()) {
      ++pos;
      aggregate = command[pos].AsString();
      if (aggregate != "SUM" && aggregate != "MIN" && aggregate != "MAX") {
        return CommandResult(false, "ERR unknown aggregate type");
      }
      ++pos;
    } else {
      ++pos;
    }
  }

  // Try to use WorkerScheduler for cross-shard query (NO SHARING architecture)
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    server::Worker* current_worker = context->GetWorker();

    // Collect all members with scores from all zsets across all workers
    std::vector<std::future<std::vector<
        std::pair<std::string, absl::flat_hash_map<std::string, double>>>>>
        futures;
    futures.reserve(all_workers.size());

    for (size_t worker_id = 0; worker_id < all_workers.size(); ++worker_id) {
      auto promise = std::make_shared<std::promise<std::vector<
          std::pair<std::string, absl::flat_hash_map<std::string, double>>>>>();
      auto future = promise->get_future();
      futures.push_back(std::move(future));

      server::Worker* target_worker = all_workers[worker_id];
      std::vector<std::string> keys_copy = keys;

      // Check if this is the current worker - execute directly to avoid
      // deadlock
      if (target_worker == current_worker) {
        // Execute directly in current thread
        std::vector<
            std::pair<std::string, absl::flat_hash_map<std::string, double>>>
            worker_zsets;
        Database* db = &target_worker->GetDataShard().GetDatabase();

        for (const auto& key : keys_copy) {
          absl::flat_hash_map<std::string, double> zset_data;
          auto members_with_scores = db->ZRangeByRank(key, 0, -1, true, false);
          for (const auto& [member, score] : members_with_scores) {
            zset_data[member] = score;
          }
          worker_zsets.push_back({key, std::move(zset_data)});
        }

        promise->set_value(worker_zsets);
      } else {
        // Execute on other worker via queue
        all_workers[worker_id]->AddTask([keys_copy, target_worker, promise]() {
          try {
            std::vector<std::pair<std::string,
                                  absl::flat_hash_map<std::string, double>>>
                worker_zsets;
            Database* db = &target_worker->GetDataShard().GetDatabase();

            for (const auto& key : keys_copy) {
              absl::flat_hash_map<std::string, double> zset_data;
              auto members_with_scores =
                  db->ZRangeByRank(key, 0, -1, true, false);
              for (const auto& [member, score] : members_with_scores) {
                zset_data[member] = score;
              }
              worker_zsets.push_back({key, std::move(zset_data)});
            }

            promise->set_value(worker_zsets);
          } catch (...) {
            promise->set_exception(std::current_exception());
          }
        });

        // Notify worker to process task immediately
        all_workers[worker_id]->NotifyTaskProcessing();
      }
    }

    // Aggregate results from all workers
    absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, double>>
        all_zsets;
    for (auto& future : futures) {
      auto worker_zsets = future.get();
      for (const auto& [key, zset_data] : worker_zsets) {
        all_zsets[key].insert(zset_data.begin(), zset_data.end());
      }
    }

    // Get all members from first set
    if (all_zsets.find(keys[0]) == all_zsets.end()) {
      return CommandResult(RespValue(RespType::kNullArray));
    }

    const auto& first_zset = all_zsets[keys[0]];
    if (first_zset.empty()) {
      return CommandResult(RespValue(RespType::kNullArray));
    }

    // Compute intersection
    std::vector<std::pair<std::string, double>> result;
    for (const auto& [member, _] : first_zset) {
      bool in_all = true;
      double aggregated_score = 0.0;

      for (size_t i = 0; i < keys.size(); ++i) {
        const auto& zset = all_zsets[keys[i]];
        auto it = zset.find(member);
        if (it == zset.end()) {
          in_all = false;
          break;
        }

        double weighted_score = it->second * weights[i];
        if (aggregate == "SUM") {
          aggregated_score += weighted_score;
        } else if (aggregate == "MIN") {
          if (i == 0 || weighted_score < aggregated_score) {
            aggregated_score = weighted_score;
          }
        } else if (aggregate == "MAX") {
          if (i == 0 || weighted_score > aggregated_score) {
            aggregated_score = weighted_score;
          }
        }
      }

      if (in_all) {
        result.emplace_back(member, aggregated_score);
      }
    }

    // Sort by score
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // Build response
    std::vector<RespValue> resp;
    for (const auto& [member, score] : result) {
      resp.emplace_back(RespValue(member));
      if (with_scores) {
        resp.emplace_back(RespValue(score));
      }
    }

    return CommandResult(RespValue(std::move(resp)));
  }

  // Fallback: single worker mode
  // Get all members from first set
  auto first_members = db->ZRangeByRank(keys[0], 0, -1, false, false);
  if (first_members.empty()) {
    return CommandResult(RespValue(RespType::kNullArray));
  }

  // Compute intersection
  std::vector<std::pair<std::string, double>> result;
  for (const auto& [member, _] : first_members) {
    bool in_all = true;
    double aggregated_score = 0.0;

    for (size_t i = 0; i < keys.size(); ++i) {
      auto score = db->ZScore(keys[i], member);
      if (!score.has_value()) {
        in_all = false;
        break;
      }

      double weighted_score = score.value() * weights[i];
      if (aggregate == "SUM") {
        aggregated_score += weighted_score;
      } else if (aggregate == "MIN") {
        if (i == 0 || weighted_score < aggregated_score) {
          aggregated_score = weighted_score;
        }
      } else if (aggregate == "MAX") {
        if (i == 0 || weighted_score > aggregated_score) {
          aggregated_score = weighted_score;
        }
      }
    }

    if (in_all) {
      result.emplace_back(member, aggregated_score);
    }
  }

  // Sort by score
  std::sort(result.begin(), result.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

  // Build response
  std::vector<RespValue> resp;
  for (const auto& [member, score] : result) {
    resp.emplace_back(RespValue(member));
    if (with_scores) {
      resp.emplace_back(RespValue(score));
    }
  }

  return CommandResult(RespValue(std::move(resp)));
}

// ZINTERCARD numkeys key [key ...] [LIMIT limit] - Return the number of members
// in the intersection
CommandResult HandleZInterCard(const astra::protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZINTERCARD' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& numkeys_arg = command[0];
  if (!numkeys_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of numkeys argument");
  }

  int64_t numkeys;
  if (!absl::SimpleAtoi(numkeys_arg.AsString(), &numkeys) || numkeys <= 0) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  if (command.ArgCount() < static_cast<size_t>(numkeys + 1)) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZINTERCARD' command");
  }

  std::vector<std::string> keys;
  for (int64_t i = 0; i < numkeys; ++i) {
    const auto& key_arg = command[static_cast<size_t>(i) + 1];
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(key_arg.AsString());
  }

  // Parse LIMIT option
  size_t limit = 0;
  size_t pos = static_cast<size_t>(numkeys) + 1;
  while (pos < command.ArgCount()) {
    const auto& opt_arg = command[pos];
    if (!opt_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of option argument");
    }

    std::string opt = opt_arg.AsString();
    if (opt == "LIMIT" && pos + 1 < command.ArgCount()) {
      ++pos;
      int64_t limit_val;
      if (!absl::SimpleAtoi(command[pos].AsString(), &limit_val) ||
          limit_val < 0) {
        return CommandResult(false, "ERR limit is not a valid integer");
      }
      limit = static_cast<size_t>(limit_val);
    }
    ++pos;
  }

  // Try to use WorkerScheduler for cross-shard query (NO SHARING architecture)
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    server::Worker* current_worker = context->GetWorker();

    // Collect all members with scores from all zsets across all workers
    std::vector<std::future<
        std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>>>>
        futures;
    futures.reserve(all_workers.size());

    for (size_t worker_id = 0; worker_id < all_workers.size(); ++worker_id) {
      auto promise = std::make_shared<std::promise<std::vector<
          std::pair<std::string, absl::flat_hash_set<std::string>>>>>();
      auto future = promise->get_future();
      futures.push_back(std::move(future));

      server::Worker* target_worker = all_workers[worker_id];
      std::vector<std::string> keys_copy = keys;

      // Check if this is the current worker - execute directly to avoid
      // deadlock
      if (target_worker == current_worker) {
        // Execute directly in current thread
        std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>>
            worker_zsets;
        Database* db = &target_worker->GetDataShard().GetDatabase();

        for (const auto& key : keys_copy) {
          absl::flat_hash_set<std::string> zset_data;
          auto members_with_scores = db->ZRangeByRank(key, 0, -1, false, false);
          for (const auto& [member, score] : members_with_scores) {
            zset_data.insert(member);
          }
          worker_zsets.push_back({key, std::move(zset_data)});
        }

        promise->set_value(worker_zsets);
      } else {
        // Execute on other worker via queue
        all_workers[worker_id]->AddTask([keys_copy, target_worker, promise]() {
          try {
            std::vector<
                std::pair<std::string, absl::flat_hash_set<std::string>>>
                worker_zsets;
            Database* db = &target_worker->GetDataShard().GetDatabase();

            for (const auto& key : keys_copy) {
              absl::flat_hash_set<std::string> zset_data;
              auto members_with_scores =
                  db->ZRangeByRank(key, 0, -1, false, false);
              for (const auto& [member, score] : members_with_scores) {
                zset_data.insert(member);
              }
              worker_zsets.push_back({key, std::move(zset_data)});
            }

            promise->set_value(worker_zsets);
          } catch (...) {
            promise->set_exception(std::current_exception());
          }
        });

        // Notify worker to process task immediately
        all_workers[worker_id]->NotifyTaskProcessing();
      }
    }

    // Aggregate results from all workers
    absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>
        all_zsets;
    for (auto& future : futures) {
      auto worker_zsets = future.get();
      for (const auto& [key, zset_data] : worker_zsets) {
        all_zsets[key].insert(zset_data.begin(), zset_data.end());
      }
    }

    // Get all members from first set
    if (all_zsets.find(keys[0]) == all_zsets.end()) {
      return CommandResult(RespValue(static_cast<int64_t>(0)));
    }

    const auto& first_zset = all_zsets[keys[0]];
    if (first_zset.empty()) {
      return CommandResult(RespValue(static_cast<int64_t>(0)));
    }

    // Count intersection
    size_t count = 0;
    for (const auto& member : first_zset) {
      bool in_all = true;
      for (size_t i = 1; i < keys.size(); ++i) {
        const auto& zset = all_zsets[keys[i]];
        if (zset.find(member) == zset.end()) {
          in_all = false;
          break;
        }
      }

      if (in_all) {
        ++count;
        if (limit > 0 && count >= limit) {
          break;
        }
      }
    }

    return CommandResult(RespValue(static_cast<int64_t>(count)));
  }

  // Fallback: single worker mode
  // Get all members from first set
  auto first_members = db->ZRangeByRank(keys[0], 0, -1, false, false);
  if (first_members.empty()) {
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  }

  // Count intersection
  size_t count = 0;
  for (const auto& [member, _] : first_members) {
    bool in_all = true;
    for (size_t i = 1; i < keys.size(); ++i) {
      if (!db->ZScore(keys[i], member).has_value()) {
        in_all = false;
        break;
      }
    }

    if (in_all) {
      ++count;
      if (limit > 0 && count >= limit) {
        break;
      }
    }
  }

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// ZUNION numkeys key [key ...] [WEIGHTS weight [weight ...]] [AGGREGATE
// SUM|MIN|MAX] - Compute the union of sorted sets
CommandResult HandleZUnion(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZUNION' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& numkeys_arg = command[0];
  if (!numkeys_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of numkeys argument");
  }

  int64_t numkeys;
  if (!absl::SimpleAtoi(numkeys_arg.AsString(), &numkeys) || numkeys <= 0) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  if (command.ArgCount() < static_cast<size_t>(numkeys + 1)) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZUNION' command");
  }

  std::vector<std::string> keys;
  for (int64_t i = 0; i < numkeys; ++i) {
    const auto& key_arg = command[static_cast<size_t>(i) + 1];
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(key_arg.AsString());
  }

  // Parse options (same as ZINTER)
  bool with_scores = false;
  std::vector<double> weights(numkeys, 1.0);
  std::string aggregate = "SUM";

  size_t pos = static_cast<size_t>(numkeys) + 1;
  while (pos < command.ArgCount()) {
    const auto& opt_arg = command[pos];
    if (!opt_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of option argument");
    }

    std::string opt = opt_arg.AsString();
    if (opt == "WITHSCORES") {
      with_scores = true;
      ++pos;
    } else if (opt == "WEIGHTS" && pos + numkeys < command.ArgCount()) {
      for (int64_t i = 0; i < numkeys; ++i) {
        ++pos;
        double weight;
        if (!absl::SimpleAtod(command[pos].AsString(), &weight)) {
          return CommandResult(false, "ERR weight is not a valid float");
        }
        weights[static_cast<size_t>(i)] = weight;
      }
      ++pos;
    } else if (opt == "AGGREGATE" && pos + 1 < command.ArgCount()) {
      ++pos;
      aggregate = command[pos].AsString();
      if (aggregate != "SUM" && aggregate != "MIN" && aggregate != "MAX") {
        return CommandResult(false, "ERR unknown aggregate type");
      }
      ++pos;
    } else {
      ++pos;
    }
  }

  // Try to use WorkerScheduler for cross-shard query (NO SHARING architecture)
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    server::Worker* current_worker = context->GetWorker();

    // Collect all members with scores from all zsets across all workers
    std::vector<std::future<std::vector<
        std::pair<std::string, absl::flat_hash_map<std::string, double>>>>>
        futures;
    futures.reserve(all_workers.size());

    for (size_t worker_id = 0; worker_id < all_workers.size(); ++worker_id) {
      auto promise = std::make_shared<std::promise<std::vector<
          std::pair<std::string, absl::flat_hash_map<std::string, double>>>>>();
      auto future = promise->get_future();
      futures.push_back(std::move(future));

      server::Worker* target_worker = all_workers[worker_id];
      std::vector<std::string> keys_copy = keys;

      // Check if this is the current worker - execute directly to avoid
      // deadlock
      if (target_worker == current_worker) {
        // Execute directly in current thread
        std::vector<
            std::pair<std::string, absl::flat_hash_map<std::string, double>>>
            worker_zsets;
        Database* db = &target_worker->GetDataShard().GetDatabase();

        for (const auto& key : keys_copy) {
          absl::flat_hash_map<std::string, double> zset_data;
          auto members_with_scores = db->ZRangeByRank(key, 0, -1, true, false);
          for (const auto& [member, score] : members_with_scores) {
            zset_data[member] = score;
          }
          worker_zsets.push_back({key, std::move(zset_data)});
        }

        promise->set_value(worker_zsets);
      } else {
        // Execute on other worker via queue
        all_workers[worker_id]->AddTask([keys_copy, target_worker, promise]() {
          try {
            std::vector<std::pair<std::string,
                                  absl::flat_hash_map<std::string, double>>>
                worker_zsets;
            Database* db = &target_worker->GetDataShard().GetDatabase();

            for (const auto& key : keys_copy) {
              absl::flat_hash_map<std::string, double> zset_data;
              auto members_with_scores =
                  db->ZRangeByRank(key, 0, -1, true, false);
              for (const auto& [member, score] : members_with_scores) {
                zset_data[member] = score;
              }
              worker_zsets.push_back({key, std::move(zset_data)});
            }

            promise->set_value(worker_zsets);
          } catch (...) {
            promise->set_exception(std::current_exception());
          }
        });

        // Notify worker to process task immediately
        all_workers[worker_id]->NotifyTaskProcessing();
      }
    }

    // Aggregate results from all workers
    absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, double>>
        all_zsets;
    for (auto& future : futures) {
      auto worker_zsets = future.get();
      for (const auto& [key, zset_data] : worker_zsets) {
        all_zsets[key].insert(zset_data.begin(), zset_data.end());
      }
    }

    // Compute union using a map
    absl::flat_hash_map<std::string, std::vector<double>> member_scores;

    for (size_t i = 0; i < keys.size(); ++i) {
      const auto& zset = all_zsets[keys[i]];
      for (const auto& [member, score] : zset) {
        member_scores[member].push_back(score * weights[i]);
      }
    }

    // Aggregate scores
    std::vector<std::pair<std::string, double>> result;
    for (const auto& [member, scores] : member_scores) {
      double aggregated_score = 0.0;
      if (aggregate == "SUM") {
        for (double s : scores) {
          aggregated_score += s;
        }
      } else if (aggregate == "MIN") {
        aggregated_score = *std::min_element(scores.begin(), scores.end());
      } else if (aggregate == "MAX") {
        aggregated_score = *std::max_element(scores.begin(), scores.end());
      }

      result.emplace_back(member, aggregated_score);
    }

    // Sort by score
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // Build response
    std::vector<RespValue> resp;
    for (const auto& [member, score] : result) {
      resp.emplace_back(RespValue(member));
      if (with_scores) {
        resp.emplace_back(RespValue(score));
      }
    }

    return CommandResult(RespValue(std::move(resp)));
  }

  // Fallback: single worker mode
  // Compute union using a map
  absl::flat_hash_map<std::string, std::vector<double>> member_scores;

  for (size_t i = 0; i < keys.size(); ++i) {
    auto members = db->ZRangeByRank(keys[i], 0, -1, false, true);
    for (const auto& [member, score] : members) {
      member_scores[member].push_back(score * weights[i]);
    }
  }

  // Aggregate scores
  std::vector<std::pair<std::string, double>> result;
  for (const auto& [member, scores] : member_scores) {
    double aggregated_score = 0.0;
    if (aggregate == "SUM") {
      for (double s : scores) {
        aggregated_score += s;
      }
    } else if (aggregate == "MIN") {
      aggregated_score = *std::min_element(scores.begin(), scores.end());
    } else if (aggregate == "MAX") {
      aggregated_score = *std::max_element(scores.begin(), scores.end());
    }

    result.emplace_back(member, aggregated_score);
  }

  // Sort by score
  std::sort(result.begin(), result.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

  // Build response
  std::vector<RespValue> resp;
  for (const auto& [member, score] : result) {
    resp.emplace_back(RespValue(member));
    if (with_scores) {
      resp.emplace_back(RespValue(score));
    }
  }

  return CommandResult(RespValue(std::move(resp)));
}

// ZLEXCOUNT key min max - Count the number of members in a sorted set between a
// given lexicographical range
CommandResult HandleZLexCount(const astra::protocol::Command& command,
                              CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZLEXCOUNT' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& min_arg = command[1];
  const auto& max_arg = command[2];

  if (!key_arg.IsBulkString() || !min_arg.IsBulkString() ||
      !max_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string min = min_arg.AsString();
  std::string max = max_arg.AsString();

  // Get all members
  auto members = db->ZRangeByRank(key, 0, -1, false, false);
  if (members.empty()) {
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  }

  // Parse min and max
  auto parse_lex =
      [](const std::string& range) -> std::pair<bool, std::string> {
    bool exclusive = false;
    std::string value;
    if (range[0] == '(') {
      exclusive = true;
      value = range.substr(1);
    } else if (range[0] == '[') {
      exclusive = false;
      value = range.substr(1);
    } else {
      exclusive = false;
      value = range;
    }
    return {exclusive, value};
  };

  auto [min_exclusive, min_value] = parse_lex(min);
  auto [max_exclusive, max_value] = parse_lex(max);

  // Count members in range
  size_t count = 0;
  for (const auto& [member, _] : members) {
    bool in_min = min_exclusive ? (member > min_value) : (member >= min_value);
    bool in_max = max_exclusive ? (member < max_value) : (member <= max_value);

    if (in_min && in_max) {
      ++count;
    }
  }

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// ZMPOP numkeys key [key ...] MIN|MAX [COUNT count] - Pop members from the
// first non-empty sorted set
CommandResult HandleZMPop(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZMPOP' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& numkeys_arg = command[0];
  if (!numkeys_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of numkeys argument");
  }

  int64_t numkeys;
  if (!absl::SimpleAtoi(numkeys_arg.AsString(), &numkeys) || numkeys <= 0) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  if (command.ArgCount() < static_cast<size_t>(numkeys + 2)) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZMPOP' command");
  }

  std::vector<std::string> keys;
  for (int64_t i = 0; i < numkeys; ++i) {
    const auto& key_arg = command[static_cast<size_t>(i) + 1];
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(key_arg.AsString());
  }

  const auto& minmax_arg = command[static_cast<size_t>(numkeys) + 1];
  if (!minmax_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of MIN|MAX argument");
  }

  std::string minmax = minmax_arg.AsString();
  if (minmax != "MIN" && minmax != "MAX") {
    return CommandResult(false, "ERR syntax error");
  }

  // Parse COUNT option
  size_t count = 1;
  size_t pos = static_cast<size_t>(numkeys) + 2;
  while (pos < command.ArgCount()) {
    const auto& opt_arg = command[pos];
    if (!opt_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of option argument");
    }

    std::string opt = opt_arg.AsString();
    if (opt == "COUNT" && pos + 1 < command.ArgCount()) {
      ++pos;
      int64_t count_val;
      if (!absl::SimpleAtoi(command[pos].AsString(), &count_val) ||
          count_val <= 0) {
        return CommandResult(false,
                             "ERR count is not a valid positive integer");
      }
      count = static_cast<size_t>(count_val);
    }
    ++pos;
  }

  // Find first non-empty sorted set
  for (const auto& key : keys) {
    auto zcard = db->ZCard(key);
    if (zcard > 0) {
      // Pop members
      std::vector<std::pair<std::string, double>> popped;
      for (size_t i = 0; i < count && i < static_cast<size_t>(zcard); ++i) {
        auto member = (minmax == "MIN") ? db->ZPopMin(key) : db->ZPopMax(key);
        if (member.has_value()) {
          popped.push_back(*member);
        }
      }

      // Build response
      std::vector<RespValue> result;
      result.emplace_back(RespValue(key));

      std::vector<RespValue> members_array;
      for (const auto& [member, score] : popped) {
        members_array.emplace_back(RespValue(member));
        members_array.emplace_back(RespValue(score));
      }
      result.emplace_back(RespValue(std::move(members_array)));

      return CommandResult(RespValue(std::move(result)));
    }
  }

  return CommandResult(RespValue(RespType::kNullArray));
}

// ZMSCORE key member [member ...] - Get the score associated with the given
// members
CommandResult HandleZMScore(const astra::protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZMSCORE' command");
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

  std::vector<RespValue> result;
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& member_arg = command[i];
    if (!member_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of member argument");
    }

    auto score = db->ZScore(key, member_arg.AsString());
    if (score.has_value()) {
      result.emplace_back(RespValue(score.value()));
    } else {
      result.emplace_back(RespValue(RespType::kNullBulkString));
    }
  }

  return CommandResult(RespValue(std::move(result)));
}

// ZRANDMEMBER key [count [WITHSCORES]] - Get one or more random members from a
// sorted set
CommandResult HandleZRandMember(const astra::protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZRANDMEMBER' command");
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
  auto all_members = db->ZRangeByRank(key, 0, -1, false, false);

  if (all_members.empty()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  // Parse arguments
  int64_t count = 1;
  bool with_scores = false;

  if (command.ArgCount() >= 2) {
    const auto& count_arg = command[1];
    if (!count_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of count argument");
    }
    if (!absl::SimpleAtoi(count_arg.AsString(), &count)) {
      return CommandResult(false,
                           "ERR value is not an integer or out of range");
    }
  }

  if (command.ArgCount() >= 3) {
    const auto& opt_arg = command[2];
    if (!opt_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of option argument");
    }
    if (opt_arg.AsString() == "WITHSCORES") {
      with_scores = true;
    }
  }

  // Get random members
  std::vector<std::pair<std::string, double>> random_members;
  if (count > 0) {
    // Get unique random members (when count is positive)
    size_t actual_count = static_cast<size_t>(
        std::min(count, static_cast<int64_t>(all_members.size())));
    std::vector<size_t> indices(all_members.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(),
                 std::mt19937(std::random_device()()));

    for (size_t i = 0; i < actual_count; ++i) {
      random_members.push_back(all_members[indices[i]]);
    }
  } else {
    // Get members with duplicates (when count is negative)
    size_t actual_count = static_cast<size_t>(-count);
    static absl::BitGen bitgen;
    for (size_t i = 0; i < actual_count; ++i) {
      size_t idx = absl::Uniform<size_t>(bitgen, 0, all_members.size());
      random_members.push_back(all_members[idx]);
    }
  }

  // Build response
  if (command.ArgCount() == 1) {
    // Single member
    return CommandResult(RespValue(random_members[0].first));
  } else {
    // Multiple members
    std::vector<RespValue> result;
    for (const auto& [member, score] : random_members) {
      result.emplace_back(RespValue(member));
      if (with_scores) {
        result.emplace_back(RespValue(score));
      }
    }
    return CommandResult(RespValue(std::move(result)));
  }
}

// ZRANGEBYLEX key min max [LIMIT offset count] - Return members in a sorted set
// by lexicographical range
CommandResult HandleZRangeByLex(const astra::protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZRANGEBYLEX' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& min_arg = command[1];
  const auto& max_arg = command[2];

  if (!key_arg.IsBulkString() || !min_arg.IsBulkString() ||
      !max_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string min = min_arg.AsString();
  std::string max = max_arg.AsString();

  // Parse LIMIT option
  size_t offset = 0;
  size_t count = -1;
  for (size_t i = 3; i < command.ArgCount(); ++i) {
    const auto& opt_arg = command[i];
    if (!opt_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of option argument");
    }

    std::string opt = opt_arg.AsString();
    if (opt == "LIMIT" && i + 2 < command.ArgCount()) {
      int64_t offset_val, count_val;
      if (!absl::SimpleAtoi(command[i + 1].AsString(), &offset_val) ||
          offset_val < 0) {
        return CommandResult(false, "ERR offset is not a valid integer");
      }
      if (!absl::SimpleAtoi(command[i + 2].AsString(), &count_val) ||
          count_val < 0) {
        return CommandResult(false, "ERR count is not a valid integer");
      }
      offset = static_cast<size_t>(offset_val);
      count = static_cast<size_t>(count_val);
      i += 2;
    }
  }

  // Get all members
  auto members = db->ZRangeByRank(key, 0, -1, false, false);
  if (members.empty()) {
    return CommandResult(RespValue(RespType::kNullArray));
  }

  // Parse min and max
  auto parse_lex =
      [](const std::string& range) -> std::pair<bool, std::string> {
    bool exclusive = false;
    std::string value;
    if (range[0] == '(') {
      exclusive = true;
      value = range.substr(1);
    } else if (range[0] == '[') {
      exclusive = false;
      value = range.substr(1);
    } else {
      exclusive = false;
      value = range;
    }
    return {exclusive, value};
  };

  auto [min_exclusive, min_value] = parse_lex(min);
  auto [max_exclusive, max_value] = parse_lex(max);

  // Filter members by range
  std::vector<std::string> filtered;
  for (const auto& [member, _] : members) {
    bool in_min = min_exclusive ? (member > min_value) : (member >= min_value);
    bool in_max = max_exclusive ? (member < max_value) : (member <= max_value);

    if (in_min && in_max) {
      filtered.push_back(member);
    }
  }

  // Apply LIMIT
  std::vector<std::string> result;
  size_t start = std::min(offset, filtered.size());
  size_t end = std::min(start + count, filtered.size());
  for (size_t i = start; i < end; ++i) {
    result.push_back(filtered[i]);
  }

  // Build response
  std::vector<RespValue> resp;
  for (const auto& member : result) {
    resp.emplace_back(RespValue(member));
  }

  return CommandResult(RespValue(std::move(resp)));
}

// ZRANGESTORE destination src min max [LIMIT offset count] - Store a range in a
// sorted set
CommandResult HandleZRangeStore(const astra::protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() < 4) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZRANGESTORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& dest_arg = command[0];
  const auto& src_arg = command[1];
  const auto& min_arg = command[2];
  const auto& max_arg = command[3];

  if (!dest_arg.IsBulkString() || !src_arg.IsBulkString() ||
      !min_arg.IsBulkString() || !max_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string dest = dest_arg.AsString();
  std::string src = src_arg.AsString();
  std::string min = min_arg.AsString();
  std::string max = max_arg.AsString();

  // Parse min and max
  auto parse_lex =
      [](const std::string& range) -> std::pair<bool, std::string> {
    bool exclusive = false;
    std::string value;
    if (range[0] == '(') {
      exclusive = true;
      value = range.substr(1);
    } else if (range[0] == '[') {
      exclusive = false;
      value = range.substr(1);
    } else {
      exclusive = false;
      value = range;
    }
    return {exclusive, value};
  };

  auto [min_exclusive, min_value] = parse_lex(min);
  auto [max_exclusive, max_value] = parse_lex(max);

  // Get all members from source
  auto members = db->ZRangeByRank(src, 0, -1, false, true);
  if (members.empty()) {
    db->Del({dest});
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  }

  // Filter members by range
  std::vector<std::pair<std::string, double>> filtered;
  for (const auto& [member, score] : members) {
    bool in_min = min_exclusive ? (member > min_value) : (member >= min_value);
    bool in_max = max_exclusive ? (member < max_value) : (member <= max_value);

    if (in_min && in_max) {
      filtered.push_back({member, score});
    }
  }

  // Delete destination and add filtered members
  db->Del({dest});
  for (const auto& [member, score] : filtered) {
    db->ZAdd(dest, score, member);
  }

  return CommandResult(RespValue(static_cast<int64_t>(filtered.size())));
}

// ZREMRANGEBYLEX key min max - Remove all members in a sorted set between the
// given lexicographical range
CommandResult HandleZRemRangeByLex(const astra::protocol::Command& command,
                                   CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZREMRANGEBYLEX' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& min_arg = command[1];
  const auto& max_arg = command[2];

  if (!key_arg.IsBulkString() || !min_arg.IsBulkString() ||
      !max_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string min = min_arg.AsString();
  std::string max = max_arg.AsString();

  // Parse min and max
  auto parse_lex =
      [](const std::string& range) -> std::pair<bool, std::string> {
    bool exclusive = false;
    std::string value;
    if (range[0] == '(') {
      exclusive = true;
      value = range.substr(1);
    } else if (range[0] == '[') {
      exclusive = false;
      value = range.substr(1);
    } else {
      exclusive = false;
      value = range;
    }
    return {exclusive, value};
  };

  auto [min_exclusive, min_value] = parse_lex(min);
  auto [max_exclusive, max_value] = parse_lex(max);

  // Get all members
  auto members = db->ZRangeByRank(key, 0, -1, false, true);
  if (members.empty()) {
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  }

  // Find members in range
  std::vector<std::string> to_remove;
  for (const auto& [member, score] : members) {
    bool in_min = min_exclusive ? (member > min_value) : (member >= min_value);
    bool in_max = max_exclusive ? (member < max_value) : (member <= max_value);

    if (in_min && in_max) {
      to_remove.push_back(member);
    }
  }

  // Remove members
  for (const auto& member : to_remove) {
    db->ZRem(key, member);
  }

  return CommandResult(RespValue(static_cast<int64_t>(to_remove.size())));
}

// ZREMRANGEBYRANK key start stop - Remove all members in a sorted set within
// the given rank range
CommandResult HandleZRemRangeByRank(const astra::protocol::Command& command,
                                    CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZREMRANGEBYRANK' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& start_arg = command[1];
  const auto& stop_arg = command[2];

  if (!key_arg.IsBulkString() || !start_arg.IsBulkString() ||
      !stop_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  int64_t start, stop;

  if (!absl::SimpleAtoi(start_arg.AsString(), &start) ||
      !absl::SimpleAtoi(stop_arg.AsString(), &stop)) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  // Get all members
  auto members = db->ZRangeByRank(key, 0, -1, false, false);
  if (members.empty()) {
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  }

  int64_t size = static_cast<int64_t>(members.size());

  // Handle negative indices
  if (start < 0) start = size + start;
  if (stop < 0) stop = size + stop;

  // Clamp to valid range
  if (start < 0) start = 0;
  if (stop >= size) stop = size - 1;
  if (start > stop) {
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  }

  // Remove members in range
  std::vector<std::string> to_remove;
  for (int64_t i = start; i <= stop; ++i) {
    if (i >= 0 && i < size) {
      to_remove.push_back(members[static_cast<size_t>(i)].first);
    }
  }

  for (const auto& member : to_remove) {
    db->ZRem(key, member);
  }

  return CommandResult(RespValue(static_cast<int64_t>(to_remove.size())));
}

// ZREMRANGEBYSCORE key min max - Remove all members in a sorted set within the
// given score range
CommandResult HandleZRemRangeByScore(const astra::protocol::Command& command,
                                     CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZREMRANGEBYSCORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& min_arg = command[1];
  const auto& max_arg = command[2];

  if (!key_arg.IsBulkString() || !min_arg.IsBulkString() ||
      !max_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string min = min_arg.AsString();
  std::string max = max_arg.AsString();

  // Parse min and max
  auto parse_score = [](const std::string& range) -> std::pair<bool, double> {
    bool exclusive = false;
    double value;
    if (range[0] == '(') {
      exclusive = true;
      if (!absl::SimpleAtod(range.substr(1), &value)) {
        value = -std::numeric_limits<double>::infinity();
      }
    } else if (range == "-inf") {
      exclusive = false;
      value = -std::numeric_limits<double>::infinity();
    } else if (range == "+inf") {
      exclusive = false;
      value = std::numeric_limits<double>::infinity();
    } else {
      exclusive = false;
      if (!absl::SimpleAtod(range, &value)) {
        value = 0.0;
      }
    }
    return {exclusive, value};
  };

  auto [min_exclusive, min_score] = parse_score(min);
  auto [max_exclusive, max_score] = parse_score(max);

  // Get all members
  auto members = db->ZRangeByRank(key, 0, -1, false, true);
  if (members.empty()) {
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  }

  // Find members in range
  std::vector<std::string> to_remove;
  for (const auto& [member, score] : members) {
    bool in_min = min_exclusive ? (score > min_score) : (score >= min_score);
    bool in_max = max_exclusive ? (score < max_score) : (score <= max_score);

    if (in_min && in_max) {
      to_remove.push_back(member);
    }
  }

  // Remove members
  for (const auto& member : to_remove) {
    db->ZRem(key, member);
  }

  return CommandResult(RespValue(static_cast<int64_t>(to_remove.size())));
}

// ZREVRANGEBYLEX key max min [LIMIT offset count] - Return members in a sorted
// set by lexicographical range (reverse)
CommandResult HandleZRevRangeByLex(const astra::protocol::Command& command,
                                   CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'ZREVRANGEBYLEX' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& max_arg = command[1];
  const auto& min_arg = command[2];

  if (!key_arg.IsBulkString() || !max_arg.IsBulkString() ||
      !min_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string max = max_arg.AsString();
  std::string min = min_arg.AsString();

  // Get all members
  auto members = db->ZRangeByRank(key, 0, -1, true, false);
  if (members.empty()) {
    return CommandResult(RespValue(RespType::kNullArray));
  }

  // Parse min and max (swapped for reverse)
  auto parse_lex =
      [](const std::string& range) -> std::pair<bool, std::string> {
    bool exclusive = false;
    std::string value;
    if (range[0] == '(') {
      exclusive = true;
      value = range.substr(1);
    } else if (range[0] == '[') {
      exclusive = false;
      value = range.substr(1);
    } else {
      exclusive = false;
      value = range;
    }
    return {exclusive, value};
  };

  auto [max_exclusive, max_value] = parse_lex(max);
  auto [min_exclusive, min_value] = parse_lex(min);

  // Filter members by range
  std::vector<std::string> filtered;
  for (const auto& [member, _] : members) {
    bool in_min = min_exclusive ? (member > min_value) : (member >= min_value);
    bool in_max = max_exclusive ? (member < max_value) : (member <= max_value);

    if (in_min && in_max) {
      filtered.push_back(member);
    }
  }

  // Build response
  std::vector<RespValue> resp;
  for (const auto& member : filtered) {
    resp.emplace_back(RespValue(member));
  }

  return CommandResult(RespValue(std::move(resp)));
}

// ZSCAN key cursor [MATCH pattern] [COUNT count] - Incrementally iterate sorted
// set elements
CommandResult HandleZScan(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'ZSCAN' command");
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

  // Get all members
  auto all_members = db->ZRangeByRank(key, 0, -1, false, true);

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
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
      }
    }
  }

  // Filter members by pattern
  std::vector<std::pair<std::string, double>> matched_members;
  for (const auto& [member, score] : all_members) {
    bool matches = false;
    if (pattern == "*") {
      matches = true;
    } else if (pattern[0] == '*' && pattern.back() == '*' &&
               pattern.size() > 1) {
      // *middle* - contains
      std::string middle = pattern.substr(1, pattern.size() - 2);
      matches = (member.find(middle) != std::string::npos);
    } else if (pattern[0] == '*' && pattern.size() > 1) {
      // *suffix - ends with
      std::string suffix = pattern.substr(1);
      matches = (member.size() >= suffix.size() &&
                 member.substr(member.size() - suffix.size()) == suffix);
    } else if (pattern.back() == '*' && pattern.size() > 1) {
      // prefix* - starts with
      std::string prefix = pattern.substr(0, pattern.size() - 1);
      matches = (member.size() >= prefix.size() &&
                 member.substr(0, prefix.size()) == prefix);
    } else {
      // exact match
      matches = (member == pattern);
    }

    if (matches) {
      matched_members.push_back({member, score});
    }
  }

  // Get current page
  std::vector<RespValue> result_array;
  size_t start = static_cast<size_t>(cursor);
  size_t end = std::min(start + count, matched_members.size());

  for (size_t i = start; i < end; ++i) {
    result_array.emplace_back(RespValue(matched_members[i].first));
    result_array.emplace_back(RespValue(matched_members[i].second));
  }

  // Build response
  std::vector<RespValue> response;

  // New cursor
  uint64_t new_cursor = (end >= matched_members.size()) ? 0 : end;
  RespValue cursor_val;
  cursor_val.SetString(std::to_string(new_cursor), RespType::kBulkString);
  response.emplace_back(cursor_val);

  // Results
  response.emplace_back(RespValue(std::move(result_array)));

  return CommandResult(RespValue(std::move(response)));
}

// New zset commands (Redis 7.0+)
ASTRADB_REGISTER_COMMAND(ZINTER, -3, "readonly", RoutingStrategy::kNone,
                         HandleZInter);
ASTRADB_REGISTER_COMMAND(ZINTERCARD, -2, "readonly", RoutingStrategy::kNone,
                         HandleZInterCard);
ASTRADB_REGISTER_COMMAND(ZUNION, -3, "readonly", RoutingStrategy::kNone,
                         HandleZUnion);
ASTRADB_REGISTER_COMMAND(ZLEXCOUNT, 4, "readonly", RoutingStrategy::kByFirstKey,
                         HandleZLexCount);
ASTRADB_REGISTER_COMMAND(ZMPOP, -4, "write", RoutingStrategy::kNone,
                         HandleZMPop);
ASTRADB_REGISTER_COMMAND(ZMSCORE, -2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleZMScore);
ASTRADB_REGISTER_COMMAND(ZRANDMEMBER, -2, "readonly",
                         RoutingStrategy::kByFirstKey, HandleZRandMember);
ASTRADB_REGISTER_COMMAND(ZRANGEBYLEX, -4, "readonly",
                         RoutingStrategy::kByFirstKey, HandleZRangeByLex);
ASTRADB_REGISTER_COMMAND(ZRANGESTORE, -4, "write", RoutingStrategy::kByFirstKey,
                         HandleZRangeStore);
ASTRADB_REGISTER_COMMAND(ZREMRANGEBYLEX, 4, "write",
                         RoutingStrategy::kByFirstKey, HandleZRemRangeByLex);
ASTRADB_REGISTER_COMMAND(ZREMRANGEBYRANK, 4, "write",
                         RoutingStrategy::kByFirstKey, HandleZRemRangeByRank);
ASTRADB_REGISTER_COMMAND(ZREMRANGEBYSCORE, 4, "write",
                         RoutingStrategy::kByFirstKey, HandleZRemRangeByScore);
ASTRADB_REGISTER_COMMAND(ZREVRANGEBYLEX, -4, "readonly",
                         RoutingStrategy::kByFirstKey, HandleZRevRangeByLex);
ASTRADB_REGISTER_COMMAND(ZSCAN, -3, "readonly", RoutingStrategy::kByFirstKey,
                         HandleZScan);

}  // namespace astra::commands
