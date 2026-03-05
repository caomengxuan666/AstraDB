// ==============================================================================
// ZSet Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "zset_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/protocol/resp/resp_builder.hpp"

namespace astra::commands {

// ZADD key score member [score member ...]
CommandResult HandleZAdd(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 3 || command.ArgCount() % 2 != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZADD' command");
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
CommandResult HandleZRange(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 3 || command.ArgCount() > 4) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZRANGE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& start_arg = command[1];
  const auto& stop_arg = command[2];

  if (!key_arg.IsBulkString() || !start_arg.IsBulkString() || !stop_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  int64_t start, stop;
  
  try {
    if (!absl::SimpleAtoi(start_arg.AsString(), &start)) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
    if (!absl::SimpleAtoi(stop_arg.AsString(), &stop)) {
      return CommandResult(false, "ERR value is not an integer or out of range");
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

  std::vector<RespValue> array;
  array.reserve(results.size());
  for (const auto& [member, score] : results) {
    if (with_scores) {
      array.emplace_back(RespValue(std::string(member)));
      array.emplace_back(RespValue(score));
    } else {
      array.emplace_back(RespValue(std::string(member)));
    }
  }

  return CommandResult(RespValue(std::move(array)));
}

// ZREM key member [member ...]
CommandResult HandleZRem(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZREM' command");
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
CommandResult HandleZScore(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZSCORE' command");
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
CommandResult HandleZCard(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZCARD' command");
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
CommandResult HandleZCount(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZCOUNT' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& min_arg = command[1];
  const auto& max_arg = command[2];

  if (!key_arg.IsBulkString() || !min_arg.IsBulkString() || !max_arg.IsBulkString()) {
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
CommandResult HandleZIncrBy(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZINCRBY' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& incr_arg = command[1];
  const auto& member_arg = command[2];

  if (!key_arg.IsBulkString() || !incr_arg.IsBulkString() || !member_arg.IsBulkString()) {
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
  std::array<absl::string_view, 3> aof_args = {key, incr_arg.AsString(), member};
  context->LogToAof("ZINCRBY", aof_args);
  
  return CommandResult(RespValue(new_score));
}

// ZRANK key member
CommandResult HandleZRank(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZRANK' command");
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
CommandResult HandleZRevRank(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZREVRANK' command");
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
CommandResult HandleBZPopMin(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'BZPOPMIN' command");
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
      std::vector<RespValue> resp;
      resp.push_back(RespValue(key));
      resp.push_back(RespValue(result->first));
      resp.push_back(RespValue(result->second));
      return CommandResult(RespValue(std::move(resp)));
    }
  }

  // All sorted sets are empty, return nil
  // TODO: Implement real blocking with timeout and wait queues
  return CommandResult(RespValue(RespType::kNullBulkString));
}

// BZPOPMAX key [key ...] timeout
// Note: This is a simplified implementation without real blocking.
// TODO: Implement real blocking with wait queues and timeout management
// TODO: Implement client notification when data becomes available
// TODO: Track blocked clients and wake them up when data is pushed
CommandResult HandleBZPopMax(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'BZPOPMAX' command");
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
      return CommandResult(RespValue(std::move(resp)));
    }
  }

  // All sorted sets are empty, return nil
  // TODO: Implement real blocking with timeout and wait queues
  return CommandResult(RespValue(RespType::kNullBulkString));
}

// BZMPOP numkeys key [key ...] timeout COUNT count [MIN|MAX]
// Note: This is a simplified implementation without real blocking.
// TODO: Implement real blocking with wait queues and timeout management
// TODO: Implement client notification when data becomes available
// TODO: Track blocked clients and wake them up when data is pushed
CommandResult HandleBZMPop(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 4) {
    return CommandResult(false, "ERR wrong number of arguments for 'BZMPOP' command");
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
    return CommandResult(false, "ERR wrong number of arguments for 'BZMPOP' command");
  }

  const auto& timeout_arg = command[timeout_idx];
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
            return CommandResult(false, "ERR count is not an integer or out of range");
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

  // All sorted sets are empty, return nil
  // TODO: Implement real blocking with timeout and wait queues
  return CommandResult(RespValue(RespType::kNullBulkString));
}

// ZREVRANGE key start stop [WITHSCORES]
CommandResult HandleZRevRange(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZREVRANGE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& start_arg = command[1];
  const auto& stop_arg = command[2];

  if (!key_arg.IsBulkString() || !start_arg.IsBulkString() || !stop_arg.IsBulkString()) {
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

  auto results = db->ZRange(key, start, stop, true, with_scores);  // reverse = true
  
  if (with_scores) {
    std::vector<RespValue> resp;
    for (const auto& [member, score] : results) {
      resp.push_back(RespValue(member));
      resp.push_back(RespValue(score));
    }
    return CommandResult(RespValue(std::move(resp)));
  } else {
    std::vector<RespValue> resp;
    for (const auto& [member, score] : results) {
      resp.push_back(RespValue(member));
    }
    return CommandResult(RespValue(std::move(resp)));
  }
}

// ZRANGEBYSCORE key min max [WITHSCORES] [LIMIT offset count]
CommandResult HandleZRangeByScore(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZRANGEBYSCORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& min_arg = command[1];
  const auto& max_arg = command[2];

  if (!key_arg.IsBulkString() || !min_arg.IsBulkString() || !max_arg.IsBulkString()) {
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

  auto results = db->ZRangeByScore(key, min_str, max_str, false, with_scores, has_limit, offset, count);
  
  if (with_scores) {
    std::vector<RespValue> resp;
    for (const auto& [member, score] : results) {
      resp.push_back(RespValue(member));
      resp.push_back(RespValue(score));
    }
    return CommandResult(RespValue(std::move(resp)));
  } else {
    std::vector<RespValue> resp;
    for (const auto& [member, score] : results) {
      resp.push_back(RespValue(member));
    }
    return CommandResult(RespValue(std::move(resp)));
  }
}

// ZREVRANGEBYSCORE key max min [WITHSCORES] [LIMIT offset count]
CommandResult HandleZRevRangeByScore(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZREVRANGEBYSCORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& max_arg = command[1];
  const auto& min_arg = command[2];

  if (!key_arg.IsBulkString() || !max_arg.IsBulkString() || !min_arg.IsBulkString()) {
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

  auto results = db->ZRangeByScore(key, min_str, max_str, true, with_scores, has_limit, offset, count);
  
  if (with_scores) {
    std::vector<RespValue> resp;
    for (const auto& [member, score] : results) {
      resp.push_back(RespValue(member));
      resp.push_back(RespValue(score));
    }
    return CommandResult(RespValue(std::move(resp)));
  } else {
    std::vector<RespValue> resp;
    for (const auto& [member, score] : results) {
      resp.push_back(RespValue(member));
    }
    return CommandResult(RespValue(std::move(resp)));
  }
}

// ZPOPMIN key [count]
CommandResult HandleZPopMin(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZPOPMIN' command");
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
        return CommandResult(false, "ERR count is not an integer or out of range");
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

  std::vector<RespValue> resp;
  for (const auto& [member, score] : results) {
    std::vector<RespValue> member_score;
    member_score.push_back(RespValue(member));
    member_score.push_back(RespValue(score));
    resp.push_back(RespValue(std::move(member_score)));
  }
  
  return CommandResult(RespValue(std::move(resp)));
}

// ZPOPMAX key [count]
CommandResult HandleZPopMax(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'ZPOPMAX' command");
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
        return CommandResult(false, "ERR count is not an integer or out of range");
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

  std::vector<RespValue> resp;
  for (const auto& [member, score] : results) {
    std::vector<RespValue> member_score;
    member_score.push_back(RespValue(member));
    member_score.push_back(RespValue(score));
    resp.push_back(RespValue(std::move(member_score)));
  }
  
  return CommandResult(RespValue(std::move(resp)));
}

// Auto-register all zset commands
ASTRADB_REGISTER_COMMAND(ZADD, -4, "write", RoutingStrategy::kByFirstKey, HandleZAdd);
ASTRADB_REGISTER_COMMAND(ZRANGE, -4, "readonly", RoutingStrategy::kByFirstKey, HandleZRange);
ASTRADB_REGISTER_COMMAND(ZREM, -3, "write", RoutingStrategy::kByFirstKey, HandleZRem);
ASTRADB_REGISTER_COMMAND(ZSCORE, 3, "readonly", RoutingStrategy::kByFirstKey, HandleZScore);
ASTRADB_REGISTER_COMMAND(ZCARD, 2, "readonly", RoutingStrategy::kByFirstKey, HandleZCard);
ASTRADB_REGISTER_COMMAND(ZCOUNT, 4, "readonly", RoutingStrategy::kByFirstKey, HandleZCount);
ASTRADB_REGISTER_COMMAND(ZINCRBY, 4, "write", RoutingStrategy::kByFirstKey, HandleZIncrBy);
ASTRADB_REGISTER_COMMAND(ZRANK, 3, "readonly", RoutingStrategy::kByFirstKey, HandleZRank);
ASTRADB_REGISTER_COMMAND(ZREVRANK, 3, "readonly", RoutingStrategy::kByFirstKey, HandleZRevRank);
ASTRADB_REGISTER_COMMAND(ZREVRANGE, -4, "readonly", RoutingStrategy::kByFirstKey, HandleZRevRange);
ASTRADB_REGISTER_COMMAND(ZRANGEBYSCORE, -4, "readonly", RoutingStrategy::kByFirstKey, HandleZRangeByScore);
ASTRADB_REGISTER_COMMAND(ZREVRANGEBYSCORE, -4, "readonly", RoutingStrategy::kByFirstKey, HandleZRevRangeByScore);
ASTRADB_REGISTER_COMMAND(ZPOPMIN, -1, "write", RoutingStrategy::kByFirstKey, HandleZPopMin);
ASTRADB_REGISTER_COMMAND(ZPOPMAX, -1, "write", RoutingStrategy::kByFirstKey, HandleZPopMax);
ASTRADB_REGISTER_COMMAND(BZPOPMIN, -2, "write", RoutingStrategy::kByFirstKey, HandleBZPopMin);
ASTRADB_REGISTER_COMMAND(BZPOPMAX, -2, "write", RoutingStrategy::kByFirstKey, HandleBZPopMax);
ASTRADB_REGISTER_COMMAND(BZMPOP, -4, "write", RoutingStrategy::kByFirstKey, HandleBZMPop);

}  // namespace astra::commands