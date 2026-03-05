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

  auto results = db->ZRange(key, start, stop, with_scores);

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

}  // namespace astra::commands