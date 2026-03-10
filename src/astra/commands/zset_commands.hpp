// ==============================================================================
// ZSet Commands Interface
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include "command_handler.hpp"

namespace astra::commands {

// ZADD key score member [score member ...]
CommandResult HandleZAdd(const astra::protocol::Command& command,
                         CommandContext* context);

// ZRANGE key start stop [WITHSCORES]
CommandResult HandleZRange(const astra::protocol::Command& command,
                           CommandContext* context);

// ZREM key member [member ...]
CommandResult HandleZRem(const astra::protocol::Command& command,
                         CommandContext* context);

// ZSCORE key member
CommandResult HandleZScore(const astra::protocol::Command& command,
                           CommandContext* context);

// ZCARD key
CommandResult HandleZCard(const astra::protocol::Command& command,
                          CommandContext* context);

// ZCOUNT key min max
CommandResult HandleZCount(const astra::protocol::Command& command,
                           CommandContext* context);

// ZINCRBY key increment member
CommandResult HandleZIncrBy(const astra::protocol::Command& command,
                            CommandContext* context);

// ZRANGE key start stop [WITHSCORES]
CommandResult HandleZRange(const astra::protocol::Command& command,
                           CommandContext* context);

// ZREVRANGE key start stop [WITHSCORES]
CommandResult HandleZRevRange(const astra::protocol::Command& command,
                              CommandContext* context);

// ZRANGEBYSCORE key min max [WITHSCORES] [LIMIT offset count]
CommandResult HandleZRangeByScore(const astra::protocol::Command& command,
                                  CommandContext* context);

// ZREVRANGEBYSCORE key max min [WITHSCORES] [LIMIT offset count]
CommandResult HandleZRevRangeByScore(const astra::protocol::Command& command,
                                     CommandContext* context);

// ZRANK key member
CommandResult HandleZRank(const astra::protocol::Command& command,
                          CommandContext* context);

// ZREVRANK key member
CommandResult HandleZRevRank(const astra::protocol::Command& command,
                             CommandContext* context);

// ZPOPMIN key [count]
CommandResult HandleZPopMin(const astra::protocol::Command& command,
                            CommandContext* context);

// ZPOPMAX key [count]
CommandResult HandleZPopMax(const astra::protocol::Command& command,
                            CommandContext* context);

// ZUNIONSTORE destination numkeys key [key ...] [WEIGHTS weight [weight ...]]
// [AGGREGATE SUM|MIN|MAX]
CommandResult HandleZUnionStore(const astra::protocol::Command& command,
                                CommandContext* context);

// ZINTERSTORE destination numkeys key [key ...] [WEIGHTS weight [weight ...]]
// [AGGREGATE SUM|MIN|MAX]
CommandResult HandleZInterStore(const astra::protocol::Command& command,
                                CommandContext* context);

// ZDIFF numkeys key [key ...] [WITHSCORES]
CommandResult HandleZDiff(const astra::protocol::Command& command,
                          CommandContext* context);

// ZDIFFSTORE destination numkeys key [key ...]
CommandResult HandleZDiffStore(const astra::protocol::Command& command,
                               CommandContext* context);

// ZSet commands are auto-registered via ASTRADB_REGISTER_COMMAND macro

}  // namespace astra::commands
