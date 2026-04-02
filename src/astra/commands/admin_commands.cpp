// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "admin_commands.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/strings/ascii.h>

#include "astra/server/worker.hpp"
#include "astra/server/worker_scheduler.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#include "acl_commands.hpp"
#include "astra/base/logging.hpp"
#include "astra/cluster/gossip_manager.hpp"
#include "astra/cluster/shard_manager.hpp"
#include "astra/core/server_stats.hpp"
#include "astra/storage/key_metadata.hpp"
#include "command_auto_register.hpp"
#include "scan_manager.hpp"

namespace astra::commands {

// Helper function to format bytes to human readable format
static std::string FormatBytes(uint64_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  int unit_index = 0;
  double size = static_cast<double>(bytes);

  while (size >= 1024.0 && unit_index < 4) {
    size /= 1024.0;
    unit_index++;
  }

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << size << units[unit_index];
  return oss.str();
}

// PING
CommandResult HandlePing(const astra::protocol::Command& command,
                         CommandContext* context) {
  RespValue pong;
  pong.SetString("PONG", protocol::RespType::kSimpleString);
  return CommandResult(pong);
}

// INFO
CommandResult HandleInfo(const astra::protocol::Command& command,
                         CommandContext* context) {
  std::ostringstream oss;

  // Get server stats (NO SHARING architecture - aggregated stats)
  auto* stats = server::ServerStatsAccessor::Instance().GetStats();

  // Server section
  oss << "# Server\r\n";
  oss << "redis_version:7.0.0\r\n";
  oss << "os:Linux\r\n";
  oss << "arch_bits:64\r\n";
  oss << "uptime_in_seconds:" << stats->uptime_seconds.load(std::memory_order_relaxed) << "\r\n";
  oss << "\r\n";

  // Clients section
  oss << "# Clients\r\n";
  oss << "connected_clients:" << stats->connected_clients.load(std::memory_order_relaxed) << "\r\n";
  oss << "total_connections_received:" << stats->total_connections_received.load(std::memory_order_relaxed) << "\r\n";
  oss << "total_connections_rejected:" << stats->total_connections_rejected.load(std::memory_order_relaxed) << "\r\n";
  oss << "\r\n";

  // Memory section
  oss << "# Memory\r\n";
  uint64_t used_memory = stats->used_memory_bytes.load(std::memory_order_relaxed);
  if (used_memory > 0) {
    oss << "used_memory:" << used_memory << "\r\n";
    oss << "used_memory_human:" << FormatBytes(used_memory) << "\r\n";
  } else {
    oss << "used_memory:0\r\n";
    oss << "used_memory_human:0B\r\n";
  }
  oss << "\r\n";

  // Persistence section
  oss << "# Persistence\r\n";
  if (context && context->IsPersistenceEnabled()) {
    oss << "enabled:yes\r\n";
    oss << "last_save:0\r\n";
  } else {
    oss << "enabled:no\r\n";
    oss << "last_save:0\r\n";
  }
  oss << "\r\n";

  // Stats section (NO SHARING architecture - from aggregated ServerStats)
  oss << "# Stats\r\n";
  oss << "total_commands_processed:" << stats->total_commands_processed.load(std::memory_order_relaxed) << "\r\n";
  oss << "total_commands_failed:" << stats->total_commands_failed.load(std::memory_order_relaxed) << "\r\n";
  oss << "keyspace_hits:" << stats->keyspace_hits.load(std::memory_order_relaxed) << "\r\n";
  oss << "keyspace_misses:" << stats->keyspace_misses.load(std::memory_order_relaxed) << "\r\n";
  oss << "slowlog_count:" << stats->slowlog_count.load(std::memory_order_relaxed) << "\r\n";
  oss << "\r\n";

  // Cluster section
  oss << "# Cluster\r\n";
  if (context && context->IsClusterEnabled()) {
    oss << "cluster_enabled:1\r\n";
    auto* gossip = context->GetGossipManager();
    if (gossip) {
      oss << "cluster_known_nodes:" << gossip->GetNodeCount() << "\r\n";
    }
  } else {
    oss << "cluster_enabled:0\r\n";
  }
  oss << "\r\n";

  // Keyspace section - Redis Insight Browser depends on this
  oss << "# Keyspace\r\n";
  if (context && context->GetDatabaseManager()) {
    auto* db_manager = context->GetDatabaseManager();
    int db_count = static_cast<int>(db_manager->GetDatabaseCount());
    for (int i = 0; i < db_count; ++i) {
      auto* db = db_manager->GetDatabase(i);
      if (db) {
        size_t key_count = db->Size();
        if (key_count > 0) {
          oss << "db" << i << ":keys=" << key_count
              << ",expires=0,avg_ttl=0,subexpiry=0\r\n";
        }
      }
    }
  }
  oss << "\r\n";

  // Command stats section (NO SHARING architecture - from aggregated ServerStats)
  oss << "# Commandstats\r\n";
  oss << stats->GetCommandStatsInfo();
  oss << "\r\n";

  RespValue response;
  response.SetString(oss.str(), protocol::RespType::kBulkString);
  return CommandResult(response);
}

// Helper function to build key specifications for a command
// This implements Redis 7.0+ key specifications format
void BuildKeySpecsHelper(std::vector<RespValue>& key_specs, int first_key,
                         int last_key, int step,
                         const std::string& name_lower) {
  // Build a single key spec based on routing strategy
  // Format: [[flags, begin_search, find_keys], ...]

  std::vector<RespValue> spec;

  // 1. Flags (array of strings)
  std::vector<RespValue> flags;

  // Determine flags based on operation type
  [[maybe_unused]] bool is_write = false;

  if (name_lower == "get" || name_lower == "mget" || name_lower == "hget" ||
      name_lower == "hgetall" || name_lower == "sismember" ||
      name_lower == "smembers" || name_lower == "zscore" ||
      name_lower == "zrange" || name_lower == "zcard" ||
      name_lower == "scard" || name_lower == "hlen" || name_lower == "strlen" ||
      name_lower == "exists" || name_lower == "type" || name_lower == "keys" ||
      name_lower == "scan" || name_lower == "sscan" || name_lower == "hscan" ||
      name_lower == "zscan") {
    // Readonly command
    RespValue flag1;
    flag1.SetString("RO", protocol::RespType::kSimpleString);
    flags.push_back(flag1);
    RespValue flag2;
    flag2.SetString("access", protocol::RespType::kSimpleString);
    flags.push_back(flag2);
  } else {
    is_write = true;
    RespValue flag1;
    flag1.SetString("RW", protocol::RespType::kSimpleString);
    flags.push_back(flag1);
    RespValue flag2;
    flag2.SetString("access", protocol::RespType::kSimpleString);
    flags.push_back(flag2);
  }

  spec.push_back(RespValue(std::move(flags)));

  // 2. begin_search (map with type and spec)
  // In RESP, maps are represented as alternating key-value pairs
  std::vector<RespValue> begin_search;

  // "begin_search" key
  RespValue bs_key;
  bs_key.SetString("begin_search", protocol::RespType::kBulkString);
  begin_search.push_back(bs_key);

  // begin_search value (map with type and spec)
  std::vector<RespValue> bs_value;

  // "type" key
  RespValue bs_type_key;
  bs_type_key.SetString("type", protocol::RespType::kBulkString);
  bs_value.push_back(bs_type_key);

  // "type" value
  RespValue bs_type_value;
  bs_type_value.SetString("index", protocol::RespType::kBulkString);
  bs_value.push_back(bs_type_value);

  // "spec" key
  RespValue bs_spec_key;
  bs_spec_key.SetString("spec", protocol::RespType::kBulkString);
  bs_value.push_back(bs_spec_key);

  // "spec" value (map with index)
  std::vector<RespValue> bs_spec_value;

  // "index" key
  RespValue bs_spec_index_key;
  bs_spec_index_key.SetString("index", protocol::RespType::kBulkString);
  bs_spec_value.push_back(bs_spec_index_key);

  // "index" value
  RespValue bs_spec_index_value;
  bs_spec_index_value.SetInteger(first_key);
  bs_spec_value.push_back(bs_spec_index_value);

  bs_value.push_back(RespValue(std::move(bs_spec_value)));

  begin_search.push_back(RespValue(std::move(bs_value)));

  spec.push_back(RespValue(std::move(begin_search)));

  // 3. find_keys (map with type and spec)
  std::vector<RespValue> find_keys;

  // "find_keys" key
  RespValue fk_key;
  fk_key.SetString("find_keys", protocol::RespType::kBulkString);
  find_keys.push_back(fk_key);

  // find_keys value (map with type and spec)
  std::vector<RespValue> fk_value;

  // "type" key
  RespValue fk_type_key;
  fk_type_key.SetString("type", protocol::RespType::kBulkString);
  fk_value.push_back(fk_type_key);

  // "type" value
  RespValue fk_type_value;

  // Determine find_keys type based on command
  if (name_lower == "mget" || name_lower == "mset" || name_lower == "del" ||
      name_lower == "sadd" || name_lower == "srem" || name_lower == "zadd" ||
      name_lower == "zrem" || name_lower == "sinter" ||
      name_lower == "sunion" || name_lower == "sdiff") {
    // Multiple keys (keynum or range)
    fk_type_value.SetString("keynum", protocol::RespType::kBulkString);
  } else {
    fk_type_value.SetString("range", protocol::RespType::kBulkString);
  }

  fk_value.push_back(fk_type_value);

  // "spec" key
  RespValue fk_spec_key;
  fk_spec_key.SetString("spec", protocol::RespType::kBulkString);
  fk_value.push_back(fk_spec_key);

  // "spec" value (map with range or keynum)
  std::vector<RespValue> fk_spec_value;

  if (fk_type_value.AsString() == "range") {
    // Range spec: lastkey, keystep, limit
    // "lastkey" key
    RespValue fk_spec_lastkey_key;
    fk_spec_lastkey_key.SetString("lastkey", protocol::RespType::kBulkString);
    fk_spec_value.push_back(fk_spec_lastkey_key);

    // "lastkey" value
    RespValue fk_spec_lastkey_value;
    fk_spec_lastkey_value.SetInteger(last_key);
    fk_spec_value.push_back(fk_spec_lastkey_value);

    // "keystep" key
    RespValue fk_spec_keystep_key;
    fk_spec_keystep_key.SetString("keystep", protocol::RespType::kBulkString);
    fk_spec_value.push_back(fk_spec_keystep_key);

    // "keystep" value
    RespValue fk_spec_keystep_value;
    fk_spec_keystep_value.SetInteger(step);
    fk_spec_value.push_back(fk_spec_keystep_value);

    // "limit" key
    RespValue fk_spec_limit_key;
    fk_spec_limit_key.SetString("limit", protocol::RespType::kBulkString);
    fk_spec_value.push_back(fk_spec_limit_key);

    // "limit" value
    RespValue fk_spec_limit_value;
    fk_spec_limit_value.SetInteger(0);
    fk_spec_value.push_back(fk_spec_limit_value);
  } else {
    // keynum spec: keynumidx, firstkey, keystep
    // "keynumidx" key
    RespValue fk_spec_keynumidx_key;
    fk_spec_keynumidx_key.SetString("keynumidx",
                                    protocol::RespType::kBulkString);
    fk_spec_value.push_back(fk_spec_keynumidx_key);

    // "keynumidx" value
    RespValue fk_spec_keynumidx_value;
    fk_spec_keynumidx_value.SetInteger(0);  // First argument contains key count
    fk_spec_value.push_back(fk_spec_keynumidx_value);

    // "firstkey" key
    RespValue fk_spec_firstkey_key;
    fk_spec_firstkey_key.SetString("firstkey", protocol::RespType::kBulkString);
    fk_spec_value.push_back(fk_spec_firstkey_key);

    // "firstkey" value
    RespValue fk_spec_firstkey_value;
    fk_spec_firstkey_value.SetInteger(1);
    fk_spec_value.push_back(fk_spec_firstkey_value);

    // "keystep" key
    RespValue fk_spec_keystep_key;
    fk_spec_keystep_key.SetString("keystep", protocol::RespType::kBulkString);
    fk_spec_value.push_back(fk_spec_keystep_key);

    // "keystep" value
    RespValue fk_spec_keystep_value;
    fk_spec_keystep_value.SetInteger(1);
    fk_spec_value.push_back(fk_spec_keystep_value);
  }

  fk_value.push_back(RespValue(std::move(fk_spec_value)));

  find_keys.push_back(RespValue(std::move(fk_value)));

  spec.push_back(RespValue(std::move(find_keys)));

  // Add the spec to key_specs if we have at least one key
  if (first_key > 0 || last_key != 0) {
    key_specs.push_back(RespValue(std::move(spec)));
  }
}

// Helper function to build command info array (Redis 6.x/7.x format)
void BuildCommandInfoArrayHelper(std::vector<RespValue>& cmd_array, auto info) {
  // 1. Command name (lowercase)
  RespValue name_val;
  std::string name_lower = info->name;
  std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
                 ::tolower);
  name_val.SetString(name_lower, protocol::RespType::kBulkString);
  cmd_array.push_back(name_val);

  // 2. Arity
  RespValue arity_val;
  arity_val.SetInteger(info->arity);
  cmd_array.push_back(arity_val);

  // 3. Flags (array of strings - Redis format, using Simple Strings)
  std::vector<RespValue> flags_array;
  for (const auto& flag : info->flags) {
    RespValue flag_val;
    flag_val.SetString(flag,
                       protocol::RespType::kSimpleString);  // Use Simple String
    flags_array.push_back(flag_val);
  }
  cmd_array.push_back(RespValue(std::move(flags_array)));

  // 4. First key, Last key, Step based on routing strategy
  int first_key = 0;
  int last_key = 0;
  int step = 0;

  switch (info->routing) {
    case RoutingStrategy::kByFirstKey:
      first_key = 1;
      last_key = 1;
      step = 1;
      break;
    case RoutingStrategy::kByArgument:
      first_key = 1;
      last_key = 1;
      step = 1;
      break;
    case RoutingStrategy::kAllShards:
      first_key = 0;
      last_key = 0;
      step = 0;
      break;
    case RoutingStrategy::kNone:
    default:
      first_key = 0;
      last_key = 0;
      step = 0;
      break;
  }

  // 5. First key
  RespValue first_key_val;
  first_key_val.SetInteger(first_key);
  cmd_array.push_back(first_key_val);

  // 6. Last key
  RespValue last_key_val;
  last_key_val.SetInteger(last_key);
  cmd_array.push_back(last_key_val);

  // 7. Key step
  RespValue step_val;
  step_val.SetInteger(step);
  cmd_array.push_back(step_val);

  // 8. Categories (array with @ prefix - Redis 6.x/7.x format, using Simple
  // Strings)
  std::vector<RespValue> categories;

  // Add category based on flags
  bool is_write = false;
  bool is_readonly = false;
  bool is_fast = false;
  bool is_slow = false;
  bool is_admin = false;

  for (const auto& flag : info->flags) {
    if (flag == "write") is_write = true;
    if (flag == "readonly") is_readonly = true;
    if (flag == "fast") is_fast = true;
    if (flag == "slow") is_slow = true;
    if (flag == "admin") is_admin = true;
  }

  // Add ACL categories (with @ prefix)
  if (is_write) {
    RespValue cat;
    cat.SetString("@write", protocol::RespType::kSimpleString);
    categories.push_back(cat);
  }
  if (is_readonly) {
    RespValue cat;
    cat.SetString("@read", protocol::RespType::kSimpleString);
    categories.push_back(cat);
  }
  if (is_fast) {
    RespValue cat;
    cat.SetString("@fast", protocol::RespType::kSimpleString);
    categories.push_back(cat);
  }
  if (is_slow) {
    RespValue cat;
    cat.SetString("@slow", protocol::RespType::kSimpleString);
    categories.push_back(cat);
  }
  if (is_admin) {
    RespValue cat;
    cat.SetString("@admin", protocol::RespType::kSimpleString);
    categories.push_back(cat);
  }

  // Add specific categories based on command name patterns
  if (name_lower == "scan" || name_lower == "sscan" || name_lower == "hscan" ||
      name_lower == "zscan") {
    RespValue cat;
    cat.SetString("@keyspace", protocol::RespType::kSimpleString);
    categories.push_back(cat);
  }

  // Add string category for string commands
  if (name_lower == "get" || name_lower == "set" || name_lower == "append" ||
      name_lower == "incr" || name_lower == "decr" || name_lower == "mget" ||
      name_lower == "mset") {
    RespValue cat;
    cat.SetString("@string", protocol::RespType::kSimpleString);
    categories.push_back(cat);
  }

  // Add list category for list commands
  if (name_lower == "lpush" || name_lower == "rpush" || name_lower == "lpop" ||
      name_lower == "rpop" || name_lower == "lrange") {
    RespValue cat;
    cat.SetString("@list", protocol::RespType::kSimpleString);
    categories.push_back(cat);
  }

  // Add hash category for hash commands
  if (name_lower == "hget" || name_lower == "hset" || name_lower == "hgetall") {
    RespValue cat;
    cat.SetString("@hash", protocol::RespType::kSimpleString);
    categories.push_back(cat);
  }

  // Add set category for set commands
  if (name_lower == "sadd" || name_lower == "srem" ||
      name_lower == "smembers") {
    RespValue cat;
    cat.SetString("@set", protocol::RespType::kSimpleString);
    categories.push_back(cat);
  }

  // Add sorted set category for zset commands
  if (name_lower == "zadd" || name_lower == "zrange" || name_lower == "zrem") {
    RespValue cat;
    cat.SetString("@sortedset", protocol::RespType::kSimpleString);
    categories.push_back(cat);
  }

  // Add stream category for stream commands
  if (name_lower == "xadd" || name_lower == "xread" || name_lower == "xrange") {
    RespValue cat;
    cat.SetString("@stream", protocol::RespType::kSimpleString);
    categories.push_back(cat);
  }

  // Add documentation category for help commands
  if (name_lower == "help") {
    RespValue cat;
    cat.SetString("@documentation", protocol::RespType::kSimpleString);
    categories.push_back(cat);
  }

  // Add at least one category
  if (categories.empty()) {
    RespValue cat;
    if (is_write) {
      cat.SetString("@write", protocol::RespType::kSimpleString);
    } else {
      cat.SetString("@read", protocol::RespType::kSimpleString);
    }
    categories.push_back(cat);
  }

  cmd_array.push_back(RespValue(std::move(categories)));

  // 9. Tips (array of strings - optional, often empty for basic commands)
  std::vector<RespValue> tips_array;

  // Add tips for SCAN command
  if (name_lower == "scan" || name_lower == "sscan" || name_lower == "hscan" ||
      name_lower == "zscan") {
    RespValue tip1;
    tip1.SetString("nondeterministic_output", protocol::RespType::kBulkString);
    tips_array.push_back(tip1);
    RespValue tip2;
    tip2.SetString("request_policy:special", protocol::RespType::kBulkString);
    tips_array.push_back(tip2);
    RespValue tip3;
    tip3.SetString("response_policy:special", protocol::RespType::kBulkString);
    tips_array.push_back(tip3);
  }

  cmd_array.push_back(RespValue(std::move(tips_array)));

  // 10. Key specifications (array - for advanced key position info)
  // Build key specifications based on command's routing strategy
  std::vector<RespValue> key_specs_array;
  BuildKeySpecsHelper(key_specs_array, first_key, last_key, step, name_lower);
  cmd_array.push_back(RespValue(std::move(key_specs_array)));

  // 11. Subcommands (array - for commands with subcommands, often empty)
  // For now, return empty array
  std::vector<RespValue> subcommands_array;
  cmd_array.push_back(RespValue(std::move(subcommands_array)));
}

// COMMAND - Redis command introspection
CommandResult HandleCommand(const astra::protocol::Command& command,
                            CommandContext* context) {
  ASTRADB_LOG_TRACE("HandleCommand: arg_count={}", command.ArgCount());

  if (command.ArgCount() == 0) {
    ASTRADB_LOG_TRACE("HandleCommand: returning full command info");
    // Return all commands in the format expected by redis-cli
    // Each command is an array: [name, arity, flags, first_key, last_key, step,
    // "", 0, [category]]
    auto* registry = context->GetCommandRegistry();
    auto command_names = registry->GetCommandNames();

    ASTRADB_LOG_TRACE("HandleCommand: got {} command names",
                      command_names.size());

    std::vector<RespValue> result;
    for (const auto& name : command_names) {
      const auto* info = registry->GetInfo(name);
      if (info) {
        std::vector<RespValue> cmd_array;
        BuildCommandInfoArrayHelper(cmd_array, info);
        result.push_back(RespValue(std::move(cmd_array)));
      }
    }

    return CommandResult(RespValue(std::move(result)));
  }

  const auto& subcommand = command[0].AsString();
  std::string upper_subcommand = absl::AsciiStrToUpper(subcommand);

  ASTRADB_LOG_TRACE(
      "HandleCommand: subcommand='{}', upper_subcommand='{}', "
      "subcommand.length()={}",
      subcommand, upper_subcommand, subcommand.length());

  if (upper_subcommand == "DOCS") {
    ASTRADB_LOG_TRACE("HandleCommand: DOCS branch");

    if (command.ArgCount() < 2) {
      return CommandResult(
          false, "ERR wrong number of arguments for 'COMMAND|DOCS' command");
    }

    const std::string& cmd_name = command[1].AsString();
    ASTRADB_LOG_TRACE("HandleCommand: DOCS for command '{}'", cmd_name);

    auto* registry = context->GetCommandRegistry();
    const auto* info = registry->GetInfo(cmd_name);

    if (!info) {
      ASTRADB_LOG_TRACE("HandleCommand: DOCS - command '{}' not found",
                        cmd_name);
      // Command not found, return null (RespType::kNullBulkString by default)
      RespValue null_result(RespType::kNullBulkString);
      return CommandResult(null_result);
    }

    // Build documentation array according to Redis COMMAND DOCS spec
    // Format: array with documentation information
    std::vector<RespValue> docs_array;

    // 1. Command name
    RespValue name_val;
    name_val.SetString(info->name, protocol::RespType::kBulkString);
    docs_array.push_back(name_val);

    // 2. Summary - build from available info
    std::ostringstream summary;
    summary << info->name << " - ";
    if (info->arity >= 0) {
      summary << "arity: " << info->arity << ", ";
    } else {
      summary << "arity: at least " << (-info->arity - 1) << ", ";
    }

    // Add flags to summary
    if (!info->flags.empty()) {
      summary << "flags: ";
      for (size_t i = 0; i < info->flags.size(); ++i) {
        if (i > 0) summary << ", ";
        summary << info->flags[i];
      }
    } else {
      summary << "no special flags";
    }

    RespValue summary_val;
    summary_val.SetString(summary.str(), protocol::RespType::kBulkString);
    docs_array.push_back(summary_val);

    // 3. Complexity
    RespValue complexity_val;
    complexity_val.SetString("O(1)", protocol::RespType::kBulkString);
    docs_array.push_back(complexity_val);

    // 4. Since (version since command was introduced)
    RespValue since_val;
    since_val.SetString("1.0.0", protocol::RespType::kBulkString);
    docs_array.push_back(since_val);

    // 5. Group (command group/category)
    RespValue group_val;
    group_val.SetString("generic", protocol::RespType::kBulkString);
    docs_array.push_back(group_val);

    // 6. Syntax (command syntax)
    std::ostringstream syntax;
    syntax << info->name;
    if (info->arity < 0) {
      syntax << " [arg ...]";
    } else {
      for (int i = 1; i < info->arity; ++i) {
        syntax << " <arg" << i << ">";
      }
    }

    RespValue syntax_val;
    syntax_val.SetString(syntax.str(), protocol::RespType::kBulkString);
    docs_array.push_back(syntax_val);

    // 7. Example
    std::ostringstream example;
    example << info->name;
    if (info->arity <= 1) {
      // No arguments or just command name
    } else {
      example << " mykey myvalue";
    }

    RespValue example_val;
    example_val.SetString(example.str(), protocol::RespType::kBulkString);
    docs_array.push_back(example_val);

    // 8. Arguments documentation (array of argument info)
    std::vector<RespValue> args_docs;
    if (info->arity < 0) {
      // Variable arguments
      RespValue arg_name;
      arg_name.SetString("args", protocol::RespType::kBulkString);
      args_docs.push_back(arg_name);

      RespValue arg_type;
      arg_type.SetString("string", protocol::RespType::kBulkString);
      args_docs.push_back(arg_type);

      RespValue arg_flags;
      arg_flags.SetString("variadic", protocol::RespType::kBulkString);
      args_docs.push_back(arg_flags);

      RespValue arg_since;
      arg_since.SetString("1.0.0", protocol::RespType::kBulkString);
      args_docs.push_back(arg_since);

      RespValue arg_summary;
      arg_summary.SetString("One or more string arguments",
                            protocol::RespType::kBulkString);
      args_docs.push_back(arg_summary);

      docs_array.push_back(RespValue(args_docs));
    }

    return CommandResult(RespValue(std::move(docs_array)));
  } else if (subcommand == "COUNT") {
    ASTRADB_LOG_TRACE("HandleCommand: COUNT branch, returning count");
    // COMMAND COUNT - return number of commands
    RespValue count;
    count.SetInteger(static_cast<int64_t>(
        RuntimeCommandRegistry::Instance().GetCommandCount()));
    return CommandResult(count);
  } else if (subcommand == "GETKEYS") {
    ASTRADB_LOG_TRACE("HandleCommand: GETKEYS branch, returning error");
    // COMMAND GETKEYS - extract keys from a command
    return CommandResult(false, "ERR COMMAND GETKEYS not implemented");
  } else if (upper_subcommand == "LIST") {
    ASTRADB_LOG_TRACE("HandleCommand: LIST branch");
    // COMMAND LIST - return list of command names
    auto* registry = context->GetCommandRegistry();
    auto command_names = registry->GetCommandNames();

    ASTRADB_LOG_TRACE("HandleCommand: LIST branch, got {} command names",
                      command_names.size());

    std::vector<RespValue> result;
    for (const auto& name : command_names) {
      RespValue name_val;
      name_val.SetString(name, protocol::RespType::kBulkString);
      result.push_back(name_val);
    }

    ASTRADB_LOG_TRACE(
        "HandleCommand: LIST branch, returning array with {} elements",
        result.size());
    return CommandResult(RespValue(std::move(result)));
  } else if (upper_subcommand == "INFO") {
    ASTRADB_LOG_TRACE("HandleCommand: INFO branch");
    // COMMAND INFO - return information about specific commands or all commands
    auto* registry = context->GetCommandRegistry();

    std::vector<RespValue> result;

    if (command.ArgCount() == 1) {
      // No command names specified, return info for all commands
      auto command_names = registry->GetCommandNames();
      for (const auto& name : command_names) {
        const auto* info = registry->GetInfo(name);
        if (info) {
          std::vector<RespValue> cmd_array;
          BuildCommandInfoArrayHelper(cmd_array, info);
          result.push_back(RespValue(std::move(cmd_array)));
        }
      }
    } else {
      // Get info for specified commands
      for (size_t i = 1; i < command.ArgCount(); ++i) {
        const std::string& cmd_name = command[i].AsString();
        const auto* info = registry->GetInfo(cmd_name);
        if (info) {
          std::vector<RespValue> cmd_array;
          BuildCommandInfoArrayHelper(cmd_array, info);
          result.push_back(RespValue(std::move(cmd_array)));
        } else {
          // Command not found, return null
          result.push_back(RespValue(RespType::kNullBulkString));
        }
      }
    }

    return CommandResult(RespValue(std::move(result)));
  }

  // Return empty array for unknown subcommands
  ASTRADB_LOG_TRACE("HandleCommand: unknown subcommand, returning empty array");
  std::vector<RespValue> result;
  return CommandResult(RespValue(result));
}

// DEBUG - Debug commands
CommandResult HandleDebug(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'debug' command");
  }

  const auto& subcommand = command[0].AsString();

  if (subcommand == "SEGFAULT") {
    // Intentionally crash (for testing)
    ASTRADB_LOG_WARN("DEBUG SEGFAULT requested");
    return CommandResult(false, "ERR SEGFAULT not implemented for safety");
  } else if (subcommand == "SLEEP") {
    // Sleep for specified seconds
    return CommandResult(false, "ERR SLEEP not implemented for safety");
  }

  return CommandResult(false, "ERR unknown debug subcommand");
}

// Helper function to format node ID as hex string
static std::string FormatNodeId(const cluster::NodeId& id) {
  return cluster::GossipManager::NodeIdToString(id);
}

// CLUSTER - Cluster management commands
CommandResult HandleCluster(const astra::protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'cluster' command");
  }

  const auto& subcommand = command[0].AsString();

  if (!context || !context->IsClusterEnabled()) {
    // Return error for cluster commands when cluster is not enabled
    if (subcommand == "INFO") {
      std::string info =
          "cluster_state:down\r\n"
          "cluster_slots_assigned:0\r\n"
          "cluster_slots_ok:0\r\n"
          "cluster_slots_pfail:0\r\n"
          "cluster_slots_fail:0\r\n"
          "cluster_known_nodes:0\r\n"
          "cluster_size:0\r\n";

      RespValue response;
      response.SetString(info, protocol::RespType::kBulkString);
      return CommandResult(response);
    }
    return CommandResult(false,
                         "ERR This instance has cluster support disabled");
  }

  auto* gossip = context->GetGossipManagerMutable();

  if (subcommand == "INFO") {
    // Return real cluster info
    auto stats =
        gossip ? gossip->GetStats() : cluster::GossipManager::GossipStats{};

    // Calculate actual slot assignment from ClusterState
    uint32_t slots_assigned = 0;
    if (auto* ctx = dynamic_cast<server::WorkerCommandContext*>(context)) {
      auto cluster_state = ctx->GetClusterState();
      if (cluster_state) {
        // Count assigned slots
        for (uint16_t slot = 0; slot < 16384; ++slot) {
          if (cluster_state->GetSlotOwner(slot).has_value()) {
            slots_assigned++;
          }
        }
      }
    }

    std::ostringstream oss;
    oss << "cluster_state:ok\r\n";
    oss << "cluster_slots_assigned:" << slots_assigned << "\r\n";
    oss << "cluster_slots_ok:" << slots_assigned << "\r\n";
    oss << "cluster_slots_pfail:0\r\n";
    oss << "cluster_slots_fail:0\r\n";
    oss << "cluster_known_nodes:" << stats.known_nodes << "\r\n";
    oss << "cluster_size:1\r\n";
    oss << "cluster_current_epoch:1\r\n";
    oss << "cluster_my_epoch:1\r\n";
    oss << "cluster_stats_messages_sent:" << stats.sent_messages << "\r\n";
    oss << "cluster_stats_messages_received:" << stats.received_messages
        << "\r\n";

    RespValue response;
    response.SetString(oss.str(), protocol::RespType::kBulkString);
    return CommandResult(response);

  } else if (subcommand == "KEYSLOT") {
    // CLUSTER KEYSLOT <key>
    // Returns the hash slot for the specified key
    if (command.ArgCount() < 2) {
      return CommandResult(
          false, "ERR wrong number of arguments for 'cluster|keyslot' command");
    }

    const auto& key = command[1].AsString();
    uint16_t slot = cluster::HashSlotCalculator::CalculateWithTag(key);

    RespValue response;
    response.SetInteger(slot);
    return CommandResult(response);

  } else if (subcommand == "NODES") {
    // Return real node info
    std::ostringstream oss;
    auto* shard_manager = context->GetClusterShardManager();

    if (gossip) {
      auto nodes = gossip->GetNodes();
      auto self = gossip->GetSelf();

      for (const auto& node : nodes) {
        // Format: <node_id> <ip:port@cport> <flags> <master> <ping_sent>
        // <pong_recv> <config_epoch> <link_state> <slots>
        std::string node_id = FormatNodeId(node.id);
        std::string flags;

        // Determine flags
        if (node.status == cluster::NodeStatus::online) {
          flags = "master";
        } else if (node.status == cluster::NodeStatus::suspect) {
          flags = "master?,fail?";
        } else if (node.status == cluster::NodeStatus::failed) {
          flags = "master,fail";
        } else {
          flags = "master";
        }

        // Mark self
        if (self.id == node.id) {
          if (!flags.empty()) flags += ",";
          flags += "myself";
        }

        oss << node_id << " ";
        oss << node.ip << ":" << node.port << "@" << (node.port + 1000) << " ";
        oss << flags << " ";
        oss << "- ";  // master (empty for masters)
        oss << "0 ";  // ping_sent
        oss << "0 ";  // pong_recv
        oss << node.config_epoch << " ";
        oss << "connected ";  // link_state

        // Add slots for self
        if (self.id == node.id && shard_manager) {
          oss << "0-16383";
        }
        oss << "\r\n";
      }

      // If no nodes, add self
      if (nodes.empty()) {
        std::string node_id = FormatNodeId(self.id);
        oss << node_id << " ";
        oss << self.ip << ":" << self.port << "@" << (self.port + 1000) << " ";
        oss << "myself,master ";
        oss << "- 0 0 1 connected 0-16383\r\n";
      }
    }

    RespValue response;
    response.SetString(oss.str(), protocol::RespType::kBulkString);
    return CommandResult(response);

  } else if (subcommand == "MEET") {
    // CLUSTER MEET <ip> <port>
    if (command.ArgCount() < 3) {
      return CommandResult(
          false, "ERR wrong number of arguments for 'cluster|meet' command");
    }

    const auto& ip = command[1].AsString();
    int port = 0;
    try {
      if (!absl::SimpleAtoi(command[2].AsString(), &port)) {
        return CommandResult(false, "ERR invalid port number");
      }
    } catch (...) {
      return CommandResult(false, "ERR invalid port number");
    }

    if (context->ClusterMeet(ip, port)) {
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);
    } else {
      return CommandResult(false, "ERR failed to meet node");
    }

  } else if (subcommand == "SLOTS") {
    // Return slot distribution
    std::vector<RespValue> result;

    auto* shard_manager = context->GetClusterShardManager();
    if (gossip && shard_manager) {
      auto self = gossip->GetSelf();
      std::string node_id = FormatNodeId(self.id);

      // Single slot range for now (all slots on this node)
      std::vector<RespValue> slot_info;

      RespValue start_slot, end_slot;
      start_slot.SetInteger(0);
      end_slot.SetInteger(16383);
      slot_info.push_back(start_slot);
      slot_info.push_back(end_slot);

      // Add node info: ip, port, node_id
      RespValue ip_val, port_val, node_id_val;
      ip_val.SetString(self.ip, protocol::RespType::kBulkString);
      port_val.SetInteger(self.port);
      node_id_val.SetString(node_id, protocol::RespType::kBulkString);
      slot_info.push_back(ip_val);
      slot_info.push_back(port_val);
      slot_info.push_back(node_id_val);

      result.push_back(RespValue(std::move(slot_info)));
    }

    return CommandResult(RespValue(std::move(result)));

  } else if (subcommand == "FORGET") {
    // CLUSTER FORGET <node_id>
    // Remove a node from the cluster
    if (command.ArgCount() < 2) {
      return CommandResult(
          false, "ERR wrong number of arguments for 'cluster|forget' command");
    }

    const auto& node_id_str = command[1].AsString();
    cluster::NodeId node_id;
    if (!cluster::GossipManager::ParseNodeId(node_id_str, node_id)) {
      return CommandResult(false, "ERR invalid node id");
    }

    auto* gossip = context->GetGossipManager();
    if (!gossip) {
      return CommandResult(false, "ERR cluster not enabled");
    }

    // Check if trying to forget self
    auto self = gossip->GetSelf();
    if (self.id == node_id) {
      return CommandResult(false,
                           "ERR I tried hard but I can't forget myself...");
    }

    // In production, we would use gossip_core_->remove_node(node_id)
    // For now, we'll just return OK
    // TODO: Implement actual node removal in GossipManager

    RespValue response;
    response.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(response);

  } else if (subcommand == "REPLICATE") {
    // CLUSTER REPLICATE <master_node_id>
    // Configure this node as a replica of the specified master node
    if (command.ArgCount() < 2) {
      return CommandResult(
          false,
          "ERR wrong number of arguments for 'cluster|replicate' command");
    }

    const auto& master_id_str = command[1].AsString();
    cluster::NodeId master_id;
    if (!cluster::GossipManager::ParseNodeId(master_id_str, master_id)) {
      return CommandResult(false, "ERR invalid node id");
    }

    auto* gossip = context->GetGossipManager();
    if (!gossip) {
      return CommandResult(false, "ERR cluster not enabled");
    }

    // Find the master node
    auto master_node = gossip->FindNode(master_id);
    if (!master_node) {
      return CommandResult(false, "ERR Unknown master node");
    }

    // Check if master is not a replica itself
    if (master_node->role == "replica") {
      return CommandResult(false, "ERR can't replicate a replica node");
    }

    // In production, we would:
    // 1. Update this node's role to replica
    // 2. Set the master_node_id
    // 3. Start replication from the master
    // 4. Notify other nodes via gossip

    // For now, we'll just return OK
    // TODO: Implement actual replication logic

    RespValue response;
    response.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(response);

  } else if (subcommand == "ADDSLOTS") {
    // CLUSTER ADDSLOTS <slot1> <slot2> ...
    if (command.ArgCount() < 2) {
      return CommandResult(
          false, "ERR wrong number of arguments for 'cluster|addslots' command");
    }

    // Parse slot numbers
    std::vector<uint16_t> slots;
    for (size_t i = 1; i < command.ArgCount(); ++i) {
      try {
        int slot = std::stoi(command[i].AsString());
        if (slot < 0 || slot >= 16384) {
          return CommandResult(false, "ERR Invalid slot number");
        }
        slots.push_back(static_cast<uint16_t>(slot));
      } catch (const std::exception& e) {
        return CommandResult(false, "ERR Invalid slot number");
      }
    }

    // Check for duplicate slots
    absl::flat_hash_set<uint16_t> slot_set(slots.begin(), slots.end());
    if (slot_set.size() != slots.size()) {
      return CommandResult(false, "ERR Duplicate slots provided");
    }

    // Add slots using context
    if (context->ClusterAddSlots(slots)) {
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);
    } else {
      return CommandResult(false, "ERR Failed to add slots");
    }

  } else if (subcommand == "DELSLOTS") {
    // CLUSTER DELSLOTS <slot1> <slot2> ...
    if (command.ArgCount() < 2) {
      return CommandResult(
          false, "ERR wrong number of arguments for 'cluster|delslots' command");
    }

    // Parse slot numbers
    std::vector<uint16_t> slots;
    for (size_t i = 1; i < command.ArgCount(); ++i) {
      try {
        int slot = std::stoi(command[i].AsString());
        if (slot < 0 || slot >= 16384) {
          return CommandResult(false, "ERR Invalid slot number");
        }
        slots.push_back(static_cast<uint16_t>(slot));
      } catch (const std::exception& e) {
        return CommandResult(false, "ERR Invalid slot number");
      }
    }

    // Check for duplicate slots
    absl::flat_hash_set<uint16_t> slot_set(slots.begin(), slots.end());
    if (slot_set.size() != slots.size()) {
      return CommandResult(false, "ERR Duplicate slots provided");
    }

    // Remove slots using context
    if (context->ClusterDelSlots(slots)) {
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);
    } else {
      return CommandResult(false, "ERR Failed to remove slots");
    }
  } else if (subcommand == "SETSLOT") {
    // CLUSTER SETSLOT <slot> IMPORTING|MIGRATING|STABLE|NODE <node_id>
    // Used during manual slot migration
    if (command.ArgCount() < 3) {
      return CommandResult(
          false, "ERR wrong number of arguments for 'cluster|setslot' command");
    }

    uint16_t slot = 0;
    try {
      int temp_slot;
      if (!absl::SimpleAtoi(command[1].AsString(), &temp_slot)) {
        return CommandResult(false, "ERR invalid slot number");
      }
      slot = static_cast<uint16_t>(temp_slot);
    } catch (...) {
      return CommandResult(false, "ERR invalid slot number");
    }

    const auto& action = command[2].AsString();
    auto* shard_manager = context->GetClusterShardManager();

    if (!shard_manager) {
      return CommandResult(false, "ERR cluster not enabled");
    }

    auto shard_id = shard_manager->GetShardForSlot(slot);

    if (action == "IMPORTING") {
      // Set this shard to importing state
      if (command.ArgCount() < 4) {
        return CommandResult(false,
                             "ERR wrong number of arguments for "
                             "'cluster|setslot importing' command");
      }
      cluster::NodeId source_node;
      if (!cluster::GossipManager::ParseNodeId(command[3].AsString(),
                                               source_node)) {
        return CommandResult(false, "ERR invalid node id");
      }
      shard_manager->StartImport(shard_id, source_node);
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);

    } else if (action == "MIGRATING") {
      // Set this shard to migrating state
      if (command.ArgCount() < 4) {
        return CommandResult(false,
                             "ERR wrong number of arguments for "
                             "'cluster|setslot migrating' command");
      }
      cluster::NodeId target_node;
      if (!cluster::GossipManager::ParseNodeId(command[3].AsString(),
                                               target_node)) {
        return CommandResult(false, "ERR invalid node id");
      }
      shard_manager->StartMigration(shard_id, target_node);
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);

    } else if (action == "STABLE") {
      // Mark slot as stable (migration complete)
      shard_manager->CompleteMigration(shard_id);
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);

    } else if (action == "NODE") {
      // Assign slot to a specific node (for cluster rebalancing)
      if (command.ArgCount() < 4) {
        return CommandResult(
            false,
            "ERR wrong number of arguments for 'cluster|setslot node' command");
      }
      // This would update the slot assignment directly
      // For now, just return OK
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);

    } else {
      return CommandResult(false, "ERR unknown setslot action");
    }

  } else if (subcommand == "GETKEYSINSLOT") {
    // CLUSTER GETKEYSINSLOT <slot> <count>
    // Returns keys in the specified slot
    if (command.ArgCount() < 3) {
      return CommandResult(
          false,
          "ERR wrong number of arguments for 'cluster|getkeysinslot' command");
    }

    [[maybe_unused]] uint16_t slot = 0;
    [[maybe_unused]] int count = 0;
    try {
      int temp_slot;
      if (!absl::SimpleAtoi(command[1].AsString(), &temp_slot)) {
        return CommandResult(false, "ERR invalid slot number");
      }
      slot = static_cast<uint16_t>(temp_slot);
      int temp_count;
      if (!absl::SimpleAtoi(command[2].AsString(), &temp_count)) {
        return CommandResult(false, "ERR invalid count number");
      }
      count = temp_count;
      // slot and count are parsed for validation; reserved for future cluster
      // slot management
    } catch (...) {
      return CommandResult(false, "ERR invalid slot or count");
    }

    // For now, return empty array - actual implementation would scan keys
    std::vector<RespValue> result;
    return CommandResult(RespValue(result));
  }

  return CommandResult(false, "ERR unknown cluster subcommand");
}

// BGSAVE - Background save (persistence)
CommandResult HandleBgSave(const astra::protocol::Command& command,
                           CommandContext* context) {
  // Check if this is WorkerCommandContext (NO SHARING architecture)
  auto* worker_ctx = dynamic_cast<astra::server::WorkerCommandContext*>(context);
  if (!worker_ctx) {
    return CommandResult(false, "ERR BGSAVE command requires WorkerCommandContext");
  }

  // Get RDB save callback
  const auto& rdb_save_callback = worker_ctx->GetRdbSaveCallback();
  if (!rdb_save_callback) {
    return CommandResult(false, "ERR RDB save callback not configured");
  }

  // Call RDB save callback (background save)
  // The callback should return "OK" on success, error message on failure
  std::string result = rdb_save_callback(true);  // true = background save

  RespValue response;
  if (result == "OK") {
    response.SetString("Background saving started",
                       protocol::RespType::kSimpleString);
    return CommandResult(response);
  } else if (result == "ALREADY_IN_PROGRESS") {
    response.SetString("Background save already in progress",
                       protocol::RespType::kSimpleString);
    return CommandResult(response);
  } else {
    response.SetString(result, protocol::RespType::kError);
    return CommandResult(false, result);
  }
}

// LASTSAVE - Last save timestamp
CommandResult HandleLastSave(const astra::protocol::Command& command,
                             CommandContext* context) {
  // Return Unix timestamp of last save
  auto now = std::chrono::system_clock::now();
  auto epoch = now.time_since_epoch();
  auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(epoch).count();

  RespValue response;
  response.SetInteger(static_cast<int64_t>(seconds));
  return CommandResult(response);
}

// SAVE - Synchronous save (persistence)
CommandResult HandleSave(const astra::protocol::Command& command,
                         CommandContext* context) {
  // Check if this is WorkerCommandContext (NO SHARING architecture)
  auto* worker_ctx = dynamic_cast<astra::server::WorkerCommandContext*>(context);
  if (!worker_ctx) {
    return CommandResult(false, "ERR SAVE command requires WorkerCommandContext");
  }

  // Get RDB save callback
  const auto& rdb_save_callback = worker_ctx->GetRdbSaveCallback();
  if (!rdb_save_callback) {
    return CommandResult(false, "ERR RDB save callback not configured");
  }

  // Call RDB save callback (blocking save)
  // The callback should return "OK" on success, error message on failure
  std::string result = rdb_save_callback(false);  // false = not background

  RespValue response;
  if (result == "OK") {
    response.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(response);
  } else {
    response.SetString(result, protocol::RespType::kError);
    return CommandResult(false, result);
  }
}

// MIGRATE - Migrate a key to another Redis instance
// MIGRATE host port key|"" destination-db timeout [COPY] [REPLACE] [AUTH
// password] [AUTH2 username password] [KEYS key [key ...]]
CommandResult HandleMigrate(const astra::protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() < 4) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'migrate' command");
  }

  // Parse arguments
  const auto& host = command[0].AsString();
  int port = 0;
  try {
    if (!absl::SimpleAtoi(command[1].AsString(), &port)) {
      return CommandResult(false, "ERR invalid port number");
    }
  } catch (...) {
    return CommandResult(false, "ERR invalid port number");
  }
  const auto& key = command[2].AsString();  // Empty string if KEYS option used
  // int destination_db = std::stoi(command[3].AsString());
  // int timeout = std::stoi(command[4].AsString());

  // Parse options
  std::vector<std::string> keys;

  if (!key.empty()) {
    keys.push_back(key);
  }

  for (size_t i = 5; i < command.ArgCount(); ++i) {
    const auto& opt = command[i].AsString();
    if (opt == "COPY") {
      // TODO: Implement COPY option (don't delete key from source)
      (void)opt;  // Suppress unused variable warning
    } else if (opt == "REPLACE") {
      // TODO: Implement REPLACE option (overwrite existing key)
      (void)opt;  // Suppress unused variable warning
    } else if (opt == "KEYS") {
      // Remaining arguments are keys
      for (size_t j = i + 1; j < command.ArgCount(); ++j) {
        keys.push_back(command[j].AsString());
      }
      break;
    }
  }

  if (keys.empty()) {
    return CommandResult(false, "ERR no key to migrate");
  }

  // Check cluster mode
  if (!context || !context->IsClusterEnabled()) {
    return CommandResult(false, "ERR MIGRATE requires cluster mode");
  }

  // In a real implementation, this would:
  // 1. Connect to target node
  // 2. Send DUMP + RESTORE or direct data transfer
  // 3. Optionally delete the key from source (unless COPY)

  // For now, return success (actual migration would be async)
  ASTRADB_LOG_INFO("MIGRATE: migrating {} keys to {}:{}", keys.size(), host,
                   port);

  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// ASKING - Indicate client is asking for a key during migration
// This is sent by clients when they receive an ASK redirect
CommandResult HandleAsking(const astra::protocol::Command& command,
                           CommandContext* context) {
  // Client is in asking mode - next command should be executed
  // even if the slot is in IMPORTING state
  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// TYPE key - Get key type
CommandResult HandleType(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'type' command");
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
  auto type = db->GetType(key);

  std::string type_str = "none";
  if (type.has_value()) {
    switch (*type) {
      case astra::storage::KeyType::kString:
        type_str = "string";
        break;
      case astra::storage::KeyType::kHash:
        type_str = "hash";
        break;
      case astra::storage::KeyType::kSet:
        type_str = "set";
        break;
      case astra::storage::KeyType::kZSet:
        type_str = "zset";
        break;
      case astra::storage::KeyType::kList:
        type_str = "list";
        break;
      case astra::storage::KeyType::kStream:
        type_str = "stream";
        break;
      default:
        type_str = "none";
        break;
    }
  }

  RespValue response;
  response.SetString(type_str, protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// KEYS pattern - Find all keys matching pattern
CommandResult HandleKeys(const astra::protocol::Command& command,
                         CommandContext* context) {
  ASTRADB_LOG_TRACE("HandleKeys: arg_count={}", command.ArgCount());

  if (command.ArgCount() != 1) {
    ASTRADB_LOG_TRACE("HandleKeys: wrong number of arguments");
    return CommandResult(false,
                         "ERR wrong number of arguments for 'keys' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    ASTRADB_LOG_TRACE("HandleKeys: database not initialized");
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& pattern_arg = command[0];
  if (!pattern_arg.IsBulkString()) {
    ASTRADB_LOG_TRACE("HandleKeys: wrong type of pattern argument");
    return CommandResult(false, "ERR wrong type of pattern argument");
  }

  std::string pattern = pattern_arg.AsString();
  ASTRADB_LOG_TRACE("HandleKeys: pattern='{}'", pattern);

  // Try to use WorkerScheduler to query all workers (NO SHARING architecture)
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler && worker_scheduler->size() > 1) {
    std::vector<RespValue> result;
    auto all_workers = worker_scheduler->GetAllWorkers();
    
    // Collect keys from all workers
    for (auto* worker : all_workers) {
      auto all_keys = worker->GetDataShard().GetDatabase().GetAllKeys();
      
      // Pattern matching for each key
      for (const auto& key : all_keys) {
        // Simple pattern matching: only support * wildcard
        if (pattern == "*" || key.find(pattern.substr(1)) != std::string::npos) {
          RespValue key_val;
          key_val.SetString(key, protocol::RespType::kBulkString);
          result.push_back(key_val);
        }
      }
    }

    ASTRADB_LOG_TRACE("HandleKeys: returning {} keys from {} workers",
                      result.size(), all_workers.size());
    return CommandResult(RespValue(std::move(result)));
  }

  // Fallback: single worker mode
  auto all_keys = db->GetAllKeys();
  ASTRADB_LOG_TRACE("HandleKeys: got {} keys", all_keys.size());

  std::vector<RespValue> result;
  for (const auto& key : all_keys) {
    // Simple pattern matching: only support * wildcard
    if (pattern == "*" || key.find(pattern.substr(1)) != std::string::npos) {
      RespValue key_val;
      key_val.SetString(key, protocol::RespType::kBulkString);
      result.push_back(key_val);
    }
  }

  ASTRADB_LOG_TRACE("HandleKeys: returning array with {} elements",
                    result.size());
  return CommandResult(RespValue(std::move(result)));
}

// RANDOMKEY - Return a random key from the database
CommandResult HandleRandomKey(const astra::protocol::Command& command,
                              CommandContext* context) {
  ASTRADB_LOG_TRACE("HandleRandomKey: arg_count={}", command.ArgCount());

  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'RANDOMKEY' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  // Try to use WorkerScheduler for cross-shard query (NO SHARING architecture)
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    server::Worker* current_worker = context->GetWorker();
    
    // Collect all keys from all workers
    std::vector<std::future<std::vector<std::string>>> futures;
    futures.reserve(all_workers.size());
    
    for (size_t worker_id = 0; worker_id < all_workers.size(); ++worker_id) {
      auto promise = std::make_shared<std::promise<std::vector<std::string>>>();
      auto future = promise->get_future();
      futures.push_back(std::move(future));
      
      server::Worker* target_worker = all_workers[worker_id];
      
      // Check if this is the current worker - execute directly to avoid deadlock
      if (target_worker == current_worker) {
        // Execute directly in current thread
        std::vector<std::string> keys = target_worker->GetDataShard().GetDatabase().GetAllKeys();
        promise->set_value(keys);
      } else {
        // Execute on other worker via queue
        all_workers[worker_id]->AddTask([target_worker, promise]() {
          try {
            std::vector<std::string> keys = target_worker->GetDataShard().GetDatabase().GetAllKeys();
            promise->set_value(keys);
          } catch (...) {
            promise->set_exception(std::current_exception());
          }
        });
        
        // Notify worker to process task immediately
        all_workers[worker_id]->NotifyTaskProcessing();
      }
    }
    
    // Aggregate all keys from all workers
    std::vector<std::string> all_keys;
    for (auto& future : futures) {
      auto keys = future.get();
      all_keys.insert(all_keys.end(), keys.begin(), keys.end());
    }
    
    if (all_keys.empty()) {
      return CommandResult(RespValue(RespType::kNullBulkString));
    }

    // Use absl::BitGen for random selection
    static absl::BitGen bitgen;
    size_t idx = absl::Uniform<size_t>(bitgen, 0, all_keys.size());

    RespValue result;
    result.SetString(all_keys[idx], protocol::RespType::kBulkString);
    return CommandResult(result);
  }

  // Fallback: single worker mode
  auto all_keys = db->GetAllKeys();
  if (all_keys.empty()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  // Use absl::BitGen for random selection
  static absl::BitGen bitgen;
  size_t idx = absl::Uniform<size_t>(bitgen, 0, all_keys.size());

  RespValue result;
  result.SetString(all_keys[idx], protocol::RespType::kBulkString);
  return CommandResult(result);
}

// RENAME key newkey - Rename a key
CommandResult HandleRename(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'RENAME' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& newkey_arg = command[1];

  if (!key_arg.IsBulkString() || !newkey_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();
  std::string newkey = newkey_arg.AsString();

  // Check if source key exists
  auto existing = db->Get(key);
  if (!existing.has_value()) {
    return CommandResult(false, "ERR no such key");
  }

  // Get the type of the source key
  auto type_opt = db->GetType(key);
  if (!type_opt) {
    return CommandResult(false, "ERR no such key");
  }

  // Copy the value to the new key (simplified - in reality, we'd need to handle
  // different types)
  db->Set(newkey, *existing);

  // Delete the old key
  db->Del({key});

  RespValue response;
  response.SetString("OK", RespType::kSimpleString);
  return CommandResult(response);
}

// RENAMENX key newkey - Rename a key, only if newkey doesn't exist
CommandResult HandleRenameNx(const astra::protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'RENAMENX' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& newkey_arg = command[1];

  if (!key_arg.IsBulkString() || !newkey_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();
  std::string newkey = newkey_arg.AsString();

  // Check if source key exists
  auto existing = db->Get(key);
  if (!existing.has_value()) {
    return CommandResult(false, "ERR no such key");
  }

  // Check if destination key already exists
  auto dest_existing = db->Get(newkey);
  if (dest_existing.has_value()) {
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  }

  // Copy the value to the new key
  db->Set(newkey, *existing);

  // Delete the old key
  db->Del({key});

  return CommandResult(RespValue(static_cast<int64_t>(1)));
}

// MOVE key db - Move a key to another database
CommandResult HandleMove(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'MOVE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& db_arg = command[1];

  if (!key_arg.IsBulkString() || !db_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string db_str = db_arg.AsString();

  // Parse database index
  int db_index;
  if (!absl::SimpleAtoi(db_str, &db_index)) {
    return CommandResult(false, "ERR invalid database index");
  }

  // For now, we don't support multiple databases in AstraDB
  return CommandResult(RespValue(static_cast<int64_t>(0)));
}

// OBJECT subcommand [arguments ...] - Inspect the internals of Redis objects
CommandResult HandleObject(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'OBJECT' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& subcommand_arg = command[0];
  const auto& key_arg = command[1];

  if (!subcommand_arg.IsBulkString() || !key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string subcommand = subcommand_arg.AsString();
  std::string key = key_arg.AsString();

  if (subcommand == "ENCODING") {
    // Return encoding of key
    auto existing = db->Get(key);
    if (!existing.has_value()) {
      return CommandResult(RespValue(RespType::kNullBulkString));
    }
    return CommandResult(RespValue("raw"));
  } else if (subcommand == "IDLETIME") {
    // Return idle time in seconds
    auto existing = db->Get(key);
    if (!existing.has_value()) {
      return CommandResult(RespValue(RespType::kNullBulkString));
    }
    // For simplicity, return 0
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  } else if (subcommand == "REFCOUNT") {
    // Return reference count
    auto existing = db->Get(key);
    if (!existing.has_value()) {
      return CommandResult(RespValue(RespType::kNullBulkString));
    }
    // For simplicity, return 1
    return CommandResult(RespValue(static_cast<int64_t>(1)));
  } else if (subcommand == "FREQ") {
    // Return access frequency
    auto existing = db->Get(key);
    if (!existing.has_value()) {
      return CommandResult(RespValue(RespType::kNullBulkString));
    }
    // For simplicity, return 0
    return CommandResult(RespValue(static_cast<int64_t>(0)));
  } else {
    return CommandResult(false, "ERR unknown OBJECT subcommand");
  }
}

// TOUCH key [key ...] - Alters the last access time of a key
CommandResult HandleTouch(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'TOUCH' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  size_t touched = 0;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& key_arg = command[i];
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }

    auto existing = db->Get(key_arg.AsString());
    if (existing.has_value()) {
      touched++;
    }
  }

  return CommandResult(RespValue(static_cast<int64_t>(touched)));
}

// DBSIZE - Return number of keys
CommandResult HandleDbSize(const astra::protocol::Command& command,
                           CommandContext* context) {
  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  // Try to use WorkerScheduler to query all workers (NO SHARING architecture)
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler && worker_scheduler->size() > 1) {
    size_t total_size = 0;
    auto all_workers = worker_scheduler->GetAllWorkers();
    
    // Collect DBSIZE from all workers
    for (auto* worker : all_workers) {
      total_size += worker->GetDataShard().GetDatabase().DbSize();
    }

    return CommandResult(RespValue(static_cast<int64_t>(total_size)));
  }

  // Fallback: single worker mode
  size_t size = db->DbSize();
  return CommandResult(RespValue(static_cast<int64_t>(size)));
}

// FLUSHDB - Clear current database
CommandResult HandleFlushDb(const astra::protocol::Command& command,
                            CommandContext* context) {
  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  db->Clear();

  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// FLUSHALL - Clear all databases
CommandResult HandleFlushAll(const astra::protocol::Command& command,
                             CommandContext* context) {
  // Get database manager and clear all databases
  DatabaseManager* db_manager = context->GetDatabaseManager();
  if (db_manager) {
    size_t db_count = db_manager->GetDatabaseCount();
    for (size_t i = 0; i < db_count; ++i) {
      Database* db = db_manager->GetDatabase(static_cast<int>(i));
      if (db) {
        db->Clear();
      }
    }
  } else {
    // Fallback: clear current database only
    Database* db = context->GetDatabase();
    if (db) {
      db->Clear();
    }
  }

  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// SELECT - Select the database with the specified zero-based numeric index
CommandResult HandleSelect(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SELECT' command");
  }

  const auto& arg = command[0];
  if (arg.GetType() != RespType::kBulkString) {
    return CommandResult(false,
                         "ERR invalid argument type for 'SELECT' command");
  }

  // Parse database index
  std::string index_str = arg.AsString();
  int db_index;
  try {
    db_index = std::stoi(index_str);
  } catch (...) {
    return CommandResult(false, "ERR invalid database index");
  }

  // Validate database index
  DatabaseManager* db_manager = context->GetDatabaseManager();
  if (db_manager) {
    if (db_index < 0 ||
        static_cast<size_t>(db_index) >= db_manager->GetDatabaseCount()) {
      return CommandResult(false, "ERR DB index is out of range");
    }
  } else {
    // Fallback: only allow index 0
    if (db_index != 0) {
      return CommandResult(false, "ERR DB index is out of range");
    }
  }

  // Set the database index
  context->SetDBIndex(db_index);

  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// CONFIG - Configuration management commands
CommandResult HandleConfig(const protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'config' command");
  }

  const auto& subcommand = command[0].AsString();
  std::string upper_subcommand = absl::AsciiStrToUpper(subcommand);

  if (upper_subcommand == "GET") {
    // CONFIG GET parameter [parameter ...]
    if (command.ArgCount() < 2) {
      return CommandResult(
          false, "ERR wrong number of arguments for 'config get' command");
    }

    std::vector<RespValue> result;

    // Support common configuration parameters
    for (size_t i = 1; i < command.ArgCount(); ++i) {
      const std::string& param = command[i].AsString();
      std::string upper_param = absl::AsciiStrToUpper(param);

      // Match parameter against supported configurations
      if (upper_param == "DATABASES" || upper_param == "*" ||
          upper_param == "*DATABASES*") {
        RespValue key;
        key.SetString("databases", protocol::RespType::kBulkString);
        result.push_back(key);

        RespValue value;
        // Get database count from DatabaseManager
        DatabaseManager* db_manager = context->GetDatabaseManager();
        int db_count =
            db_manager ? static_cast<int>(db_manager->GetDatabaseCount()) : 16;
        value.SetString(absl::StrCat(db_count),
                        protocol::RespType::kBulkString);
        result.push_back(value);
      } else if (upper_param == "IO-THREADS" || upper_param == "*" ||
                 upper_param == "*IO*THREADS*") {
        RespValue key;
        key.SetString("io-threads", protocol::RespType::kBulkString);
        result.push_back(key);

        RespValue value;
        value.SetString(
            "1",
            protocol::RespType::kBulkString);  // AstraDB uses single thread
        result.push_back(value);
      } else if (upper_param == "MAXMEMORY" || upper_param == "*" ||
                 upper_param == "*MAXMEMORY*") {
        RespValue key;
        key.SetString("maxmemory", protocol::RespType::kBulkString);
        result.push_back(key);

        RespValue value;
        // Get maxmemory from memory tracker (0 = no limit)
        uint64_t max_memory = 0;
        if (context->GetDatabase() && context->GetDatabase()->GetMemoryTracker()) {
          max_memory = context->GetDatabase()->GetMemoryTracker()->GetMaxMemory();
        }
        value.SetString(absl::StrCat(max_memory), protocol::RespType::kBulkString);
        result.push_back(value);
      } else if (upper_param == "MAXMEMORY-POLICY" || upper_param == "*" ||
                 upper_param == "*MAXMEMORY-POLICY*") {
        RespValue key;
        key.SetString("maxmemory-policy", protocol::RespType::kBulkString);
        result.push_back(key);

        RespValue value;
        // Get eviction policy from memory tracker
        std::string policy = "noeviction";
        if (context->GetDatabase() && context->GetDatabase()->GetMemoryTracker()) {
          auto astra_policy = context->GetDatabase()->GetMemoryTracker()->GetEvictionPolicy();
          policy = astra::core::memory::EvictionPolicyToString(astra_policy);
        }
        value.SetString(policy, protocol::RespType::kBulkString);
        result.push_back(value);
      } else if (upper_param == "MAXMEMORY-SAMPLES" || upper_param == "*" ||
                 upper_param == "*MAXMEMORY-SAMPLES*") {
        RespValue key;
        key.SetString("maxmemory-samples", protocol::RespType::kBulkString);
        result.push_back(key);

        RespValue value;
        // Get eviction samples from memory tracker
        uint32_t samples = 5;
        if (context->GetDatabase() && context->GetDatabase()->GetMemoryTracker()) {
          samples = context->GetDatabase()->GetMemoryTracker()->GetEvictionSamples();
        }
        value.SetString(absl::StrCat(samples), protocol::RespType::kBulkString);
        result.push_back(value);
      } else if (upper_param == "PORT" || upper_param == "*" ||
                 upper_param == "*PORT*") {
        RespValue key;
        key.SetString("port", protocol::RespType::kBulkString);
        result.push_back(key);

        RespValue value;
        value.SetString("6379", protocol::RespType::kBulkString);
        result.push_back(value);
      }
      // Add more parameters as needed
    }

    return CommandResult(RespValue(std::move(result)));
  } else {
    return CommandResult(false, "ERR unknown subcommand '" + subcommand + "'");
  }
}

// MODULE - Module management commands
CommandResult HandleModule(const protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'module' command");
  }

  const auto& subcommand = command[0].AsString();
  std::string upper_subcommand = absl::AsciiStrToUpper(subcommand);

  if (upper_subcommand == "LIST") {
    // MODULE LIST - return list of loaded modules
    // AstraDB doesn't support modules yet, return empty array
    std::vector<RespValue> result;
    return CommandResult(RespValue(std::move(result)));
  } else {
    return CommandResult(false, "ERR unknown subcommand '" + subcommand + "'");
  }
}

// SCAN - Incrementally iterate the keyspace
CommandResult HandleScan(const protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'scan' command");
  }

  // Parse cursor
  uint64_t cursor = 0;
  try {
    if (!absl::SimpleAtoi(command[0].AsString(), &cursor)) {
      return CommandResult(false, "ERR invalid cursor");
    }
  } catch (...) {
    return CommandResult(false, "ERR invalid cursor");
  }

  // Parse options
  std::string match_pattern = "*";
  int count = 10;

  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const std::string& arg = command[i].AsString();
    std::string upper_arg = absl::AsciiStrToUpper(arg);

    if (upper_arg == "MATCH" && i + 1 < command.ArgCount()) {
      match_pattern = command[++i].AsString();
    } else if (upper_arg == "COUNT" && i + 1 < command.ArgCount()) {
      try {
        count = std::stoi(command[++i].AsString());
        if (count < 1) count = 10;
      } catch (...) {
        return CommandResult(false, "ERR invalid count");
      }
    }
    // TYPE option is not implemented yet
  }

  // Get database
  auto db = context->GetDatabase();
  if (!db) {
    std::vector<RespValue> result;
    RespValue cursor_val;
    cursor_val.SetString("0", protocol::RespType::kBulkString);
    result.push_back(cursor_val);

    RespValue keys_val;
    keys_val.SetArray({});
    result.push_back(keys_val);

    return CommandResult(RespValue(std::move(result)));
  }

  // Try to use WorkerScheduler for cross-shard query (NO SHARING architecture)
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    server::Worker* current_worker = context->GetWorker();
    
    // Collect all keys from all workers
    std::vector<std::future<std::vector<std::string>>> futures;
    futures.reserve(all_workers.size());
    
    for (size_t worker_id = 0; worker_id < all_workers.size(); ++worker_id) {
      auto promise = std::make_shared<std::promise<std::vector<std::string>>>();
      auto future = promise->get_future();
      futures.push_back(std::move(future));
      
      server::Worker* target_worker = all_workers[worker_id];
      
      // Check if this is the current worker - execute directly to avoid deadlock
      if (target_worker == current_worker) {
        // Execute directly in current thread
        std::vector<std::string> keys = target_worker->GetDataShard().GetDatabase().GetAllKeys();
        promise->set_value(keys);
      } else {
        // Execute on other worker via queue
        all_workers[worker_id]->AddTask([target_worker, promise]() {
          try {
            std::vector<std::string> keys = target_worker->GetDataShard().GetDatabase().GetAllKeys();
            promise->set_value(keys);
          } catch (...) {
            promise->set_exception(std::current_exception());
          }
        });
        
        // Notify worker to process task immediately
        all_workers[worker_id]->NotifyTaskProcessing();
      }
    }
    
    // Aggregate all keys from all workers
    std::vector<std::string> keys;
    for (auto& future : futures) {
      auto worker_keys = future.get();
      keys.insert(keys.end(), worker_keys.begin(), worker_keys.end());
    }
    
    // Filter keys by pattern
    std::vector<std::string> filtered_keys;
    for (const auto& key : keys) {
      // Glob pattern matching
      bool matches = false;
      if (match_pattern == "*") {
        matches = true;
      } else if (match_pattern.find('*') == std::string::npos &&
                 match_pattern.find('?') == std::string::npos) {
        matches = (key == match_pattern);
      } else {
        std::string pattern = match_pattern;
        std::string target = key;

        if (pattern[0] == '*' && pattern.back() == '*' && pattern.size() > 1) {
          std::string middle = pattern.substr(1, pattern.size() - 2);
          matches = (target.find(middle) != std::string::npos);
        } else if (pattern[0] == '*' && pattern.size() > 1) {
          std::string suffix = pattern.substr(1);
          matches = (target.size() >= suffix.size() &&
                     target.substr(target.size() - suffix.size()) == suffix);
        } else if (pattern.back() == '*' && pattern.size() > 1) {
          std::string prefix = pattern.substr(0, pattern.size() - 1);
          matches = (target.size() >= prefix.size() &&
                     target.substr(0, prefix.size()) == prefix);
        } else {
          matches = (target.find(pattern) != std::string::npos);
        }
      }

      if (matches) {
        filtered_keys.push_back(key);
      }
    }

    // Use scan state manager for proper cursor-based iteration
    uint64_t new_cursor = 0;
    std::vector<std::string> batch_keys;

    if (cursor == 0) {
      // Start new scan
      new_cursor = ScanStateManager::Instance().StartScan(filtered_keys);
      batch_keys =
          ScanStateManager::Instance().GetNextBatch(new_cursor, count).second;
    } else {
      // Continue existing scan
      auto result = ScanStateManager::Instance().GetNextBatch(cursor, count);
      new_cursor = result.first;
      batch_keys = result.second;
    }

    // Build response
    std::vector<RespValue> response;

    // New cursor
    RespValue cursor_val;
    cursor_val.SetString(std::to_string(new_cursor),
                         protocol::RespType::kBulkString);
    response.push_back(cursor_val);

    // Keys array
    std::vector<RespValue> keys_array;
    for (const auto& key : batch_keys) {
      RespValue key_val;
      key_val.SetString(key, protocol::RespType::kBulkString);
      keys_array.push_back(key_val);
    }

    RespValue keys_val;
    keys_val.SetArray(std::move(keys_array));
    response.push_back(keys_val);

    return CommandResult(RespValue(std::move(response)));
  }

  // Fallback: single worker mode
  // Get all keys and filter by pattern
  auto keys = db->GetAllKeys();

  std::vector<std::string> filtered_keys;
  for (const auto& key : keys) {
    // Glob pattern matching
    bool matches = false;
    if (match_pattern == "*") {
      matches = true;
    } else if (match_pattern.find('*') == std::string::npos &&
               match_pattern.find('?') == std::string::npos) {
      matches = (key == match_pattern);
    } else {
      std::string pattern = match_pattern;
      std::string target = key;

      if (pattern[0] == '*' && pattern.back() == '*' && pattern.size() > 1) {
        std::string middle = pattern.substr(1, pattern.size() - 2);
        matches = (target.find(middle) != std::string::npos);
      } else if (pattern[0] == '*' && pattern.size() > 1) {
        std::string suffix = pattern.substr(1);
        matches = (target.size() >= suffix.size() &&
                   target.substr(target.size() - suffix.size()) == suffix);
      } else if (pattern.back() == '*' && pattern.size() > 1) {
        std::string prefix = pattern.substr(0, pattern.size() - 1);
        matches = (target.size() >= prefix.size() &&
                   target.substr(0, prefix.size()) == prefix);
      } else {
        matches = (target.find(pattern) != std::string::npos);
      }
    }

    if (matches) {
      filtered_keys.push_back(key);
    }
  }

  // Use scan state manager for proper cursor-based iteration
  uint64_t new_cursor = 0;
  std::vector<std::string> batch_keys;

  if (cursor == 0) {
    // Start new scan
    new_cursor = ScanStateManager::Instance().StartScan(filtered_keys);
    batch_keys =
        ScanStateManager::Instance().GetNextBatch(new_cursor, count).second;
  } else {
    // Continue existing scan
    auto result = ScanStateManager::Instance().GetNextBatch(cursor, count);
    new_cursor = result.first;
    batch_keys = result.second;
  }

  // Build response
  std::vector<RespValue> response;

  // New cursor
  RespValue cursor_val;
  cursor_val.SetString(std::to_string(new_cursor),
                       protocol::RespType::kBulkString);
  response.push_back(cursor_val);

  // Keys array
  std::vector<RespValue> keys_array;
  for (const auto& key : batch_keys) {
    RespValue key_val;
    key_val.SetString(key, protocol::RespType::kBulkString);
    keys_array.push_back(key_val);
  }

  RespValue keys_val;
  keys_val.SetArray(std::move(keys_array));
  response.push_back(keys_val);

  return CommandResult(RespValue(std::move(response)));
}

// MEMORY - Memory introspection commands
CommandResult HandleMemory(const protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'memory' command");
  }

  const auto& subcommand = command[0].AsString();
  std::string upper_subcommand = absl::AsciiStrToUpper(subcommand);

  if (upper_subcommand == "USAGE") {
    // MEMORY USAGE key [SAMPLES count]
    if (command.ArgCount() < 2) {
      return CommandResult(
          false, "ERR wrong number of arguments for 'memory|usage' command");
    }

    const std::string& key = command[1].AsString();
    auto db = context->GetDatabase();
    if (!db) {
      return CommandResult(
          protocol::RespValue(protocol::RespType::kNullBulkString));
    }

    auto key_type = db->GetType(key);
    if (!key_type.has_value()) {
      return CommandResult(
          protocol::RespValue(protocol::RespType::kNullBulkString));
    }

    // Estimate memory usage (simplified implementation)
    // This is a rough estimate - actual Redis does more sophisticated
    // calculations
    size_t key_size = key.size();
    size_t value_size = 0;
    size_t overhead = 56;  // Base Redis object overhead

    // Get approximate size based on type
    switch (key_type.value()) {
      case storage::KeyType::kString: {
        auto value = db->Get(key);
        if (value.has_value()) {
          value_size = value.value().value.size();
        }
        break;
      }
      case storage::KeyType::kHash: {
        // Get hash size
        auto hash_size = db->HLen(key);
        // Estimate: each field/value pair ~50 bytes on average
        value_size = hash_size * 50;
        break;
      }
      case storage::KeyType::kList: {
        // Get list size
        auto list_size = db->LLen(key);
        // Estimate: each element ~30 bytes on average
        value_size = list_size * 30;
        break;
      }
      case storage::KeyType::kSet: {
        // Get set size
        auto set_size = db->SCard(key);
        // Estimate: each element ~30 bytes on average
        value_size = set_size * 30;
        break;
      }
      case storage::KeyType::kZSet: {
        // Get zset size
        auto zset_size = db->ZCard(key);
        // Estimate: each member+score pair ~50 bytes on average
        value_size = zset_size * 50;
        break;
      }
      case storage::KeyType::kStream:
        // Stream - rough estimate
        value_size = 1024;  // 1KB overhead
        break;
      default:
        break;
    }

    // Add Redis internal overhead
    size_t total_size = key_size + value_size + overhead;

    // Return as integer
    protocol::RespValue resp;
    resp.SetInteger(static_cast<int64_t>(total_size));
    return CommandResult(resp);

  } else if (upper_subcommand == "STATS") {
    // MEMORY STATS - Return memory usage statistics
    std::vector<RespValue> result;

    // Basic memory statistics (simplified implementation)
    // peak.allocated
    RespValue peak_allocated_key;
    peak_allocated_key.SetString("peak.allocated",
                                 protocol::RespType::kBulkString);
    result.push_back(peak_allocated_key);
    RespValue peak_allocated_val;
    peak_allocated_val.SetInteger(0);  // TODO: Implement peak memory tracking
    result.push_back(peak_allocated_val);

    // total.allocated
    RespValue total_allocated_key;
    total_allocated_key.SetString("total.allocated",
                                  protocol::RespType::kBulkString);
    result.push_back(total_allocated_key);
    RespValue total_allocated_val;
    total_allocated_val.SetInteger(
        0);  // TODO: Implement actual memory tracking
    result.push_back(total_allocated_val);

    // startup.allocated
    RespValue startup_allocated_key;
    startup_allocated_key.SetString("startup.allocated",
                                    protocol::RespType::kBulkString);
    result.push_back(startup_allocated_key);
    RespValue startup_allocated_val;
    startup_allocated_val.SetInteger(1024 * 1024);  // Approximate 1MB
    result.push_back(startup_allocated_val);

    // replication.backlog
    RespValue replication_backlog_key;
    replication_backlog_key.SetString("replication.backlog",
                                      protocol::RespType::kBulkString);
    result.push_back(replication_backlog_key);
    RespValue replication_backlog_val;
    replication_backlog_val.SetInteger(0);  // No replication backlog
    result.push_back(replication_backlog_val);

    // keys.count
    RespValue keys_count_key;
    keys_count_key.SetString("keys.count", protocol::RespType::kBulkString);
    result.push_back(keys_count_key);
    RespValue keys_count_val;
    auto db = context->GetDatabase();
    size_t key_count = db ? db->Size() : 0;
    keys_count_val.SetInteger(static_cast<int64_t>(key_count));
    result.push_back(keys_count_val);

    // dataset.bytes
    RespValue dataset_bytes_key;
    dataset_bytes_key.SetString("dataset.bytes",
                                protocol::RespType::kBulkString);
    result.push_back(dataset_bytes_key);
    RespValue dataset_bytes_val;
    dataset_bytes_val.SetInteger(0);  // TODO: Implement actual dataset tracking
    result.push_back(dataset_bytes_val);

    // overhead.total
    RespValue overhead_total_key;
    overhead_total_key.SetString("overhead.total",
                                 protocol::RespType::kBulkString);
    result.push_back(overhead_total_key);
    RespValue overhead_total_val;
    overhead_total_val.SetInteger(1024 * 1024);  // Approximate overhead
    result.push_back(overhead_total_val);

    return CommandResult(RespValue(std::move(result)));

  } else if (upper_subcommand == "HELP") {
    // MEMORY HELP - Show help text
    std::string help_text =
        "MEMORY <subcommand> [<arg> [value] ...]. Subcommands are:\n"
        "USAGE     <key> [SAMPLES <count>] - Estimate memory usage of a key\n"
        "STATS                             - Show memory usage statistics\n"
        "HELP                              - Show this help text";
    protocol::RespValue resp;
    resp.SetString(help_text, protocol::RespType::kBulkString);
    return CommandResult(resp);

  } else {
    return CommandResult(false, "ERR unknown subcommand '" + subcommand + "'");
  }
}

// TIME - Return the current server time
CommandResult HandleTime(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'TIME' command");
  }

  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(duration).count();
  auto microseconds =
      std::chrono::duration_cast<std::chrono::microseconds>(duration).count() %
      1000000;

  std::vector<RespValue> result;

  RespValue sec_val;
  sec_val.SetInteger(static_cast<int64_t>(seconds));
  result.push_back(sec_val);

  RespValue usec_val;
  usec_val.SetInteger(static_cast<int64_t>(microseconds));
  result.push_back(usec_val);

  return CommandResult(RespValue(std::move(result)));
}

// SHUTDOWN [NOSAVE | SAVE] - Shutdown the server
CommandResult HandleShutdown(const astra::protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() > 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'SHUTDOWN' command");
  }

  // Note: In a real implementation, this would gracefully shutdown the server
  // For now, we just return OK
  // In a distributed system, we might need to signal the main server process

  RespValue result;
  result.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(result);
}

// SWAPDB index1 index2 - Swap two databases
CommandResult HandleSwapDb(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SWAPDB' command");
  }

  const auto& db1_arg = command[0];
  const auto& db2_arg = command[1];

  if (!db1_arg.IsBulkString() || !db2_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  int64_t db1, db2;
  if (!absl::SimpleAtoi(db1_arg.AsString(), &db1) ||
      !absl::SimpleAtoi(db2_arg.AsString(), &db2)) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  if (db1 < 0 || db2 < 0) {
    return CommandResult(false, "ERR DB index is out of range");
  }

  // Note: In a real implementation, this would swap the databases
  // For now, we just return OK as we have a single database manager
  // This would require DatabaseManager to support swapping databases

  RespValue result;
  result.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(result);
}

// WAIT numreplicas timeout - Wait for replicas to acknowledge writes
CommandResult HandleWait(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'WAIT' command");
  }

  const auto& replicas_arg = command[0];
  const auto& timeout_arg = command[1];

  if (!replicas_arg.IsBulkString() || !timeout_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  int64_t num_replicas, timeout_ms;
  if (!absl::SimpleAtoi(replicas_arg.AsString(), &num_replicas) ||
      !absl::SimpleAtoi(timeout_arg.AsString(), &timeout_ms)) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  // Note: In a real implementation with replication, this would wait for
  // replicas For now, we return 0 as there are no replicas
  return CommandResult(RespValue(static_cast<int64_t>(0)));
}

// WAITAOF numlocal numreplicas timeout - Wait for AOF fsync on replicas
CommandResult HandleWaitAOF(const astra::protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'WAITAOF' command");
  }

  const auto& numlocal_arg = command[0];
  const auto& numreplicas_arg = command[1];
  const auto& timeout_arg = command[2];

  if (!numlocal_arg.IsBulkString() || !numreplicas_arg.IsBulkString() ||
      !timeout_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  int64_t numlocal, numreplicas, timeout_ms;
  if (!absl::SimpleAtoi(numlocal_arg.AsString(), &numlocal) ||
      !absl::SimpleAtoi(numreplicas_arg.AsString(), &numreplicas) ||
      !absl::SimpleAtoi(timeout_arg.AsString(), &timeout_ms)) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  // Note: In a real implementation with AOF and replication, this would wait
  // for AOF fsync For now, we return 0 as there are no replicas
  return CommandResult(RespValue(static_cast<int64_t>(0)));
}

// READONLY - Enable read-only mode for replica connection
CommandResult HandleReadonly(const astra::protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'READONLY' command");
  }

  // Note: In a real implementation with replicas, this would mark the
  // connection as read-only For now, we just return OK
  RespValue result;
  result.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(result);
}

// READWRITE - Enable read-write mode for replica connection
CommandResult HandleReadwrite(const astra::protocol::Command& command,
                              CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'READWRITE' command");
  }

  // Note: In a real implementation with replicas, this would mark the
  // connection as read-write For now, we just return OK
  RespValue result;
  result.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(result);
}

// RESET - Reset the connection (flush all data)
CommandResult HandleReset(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'RESET' command");
  }

  // Note: In a real implementation, this would reset the connection state
  // For now, we just return OK
  RespValue result;
  result.SetString("RESET", protocol::RespType::kSimpleString);
  return CommandResult(result);
}

// RESTORE-ASKING - Enable next command to be sent to any node (after asking
// redirection)
CommandResult HandleRestoreAsking(const astra::protocol::Command& command,
                                  CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'RESTORE-ASKING' command");
  }

  // Note: In a real implementation with cluster, this would clear the asking
  // flag For now, we just return OK
  RespValue result;
  result.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(result);
}

// BGREWRITEAOF - Asynchronously rewrite the append-only file
CommandResult HandleBgRewriteAof(const astra::protocol::Command& command,
                                 CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'BGREWRITEAOF' command");
  }

  // Note: In a real implementation with AOF, this would trigger an AOF rewrite
  // For now, we just return a background task status
  RespValue result;
  result.SetString("Background append only file rewriting started",
                   protocol::RespType::kSimpleString);
  return CommandResult(result);
}

// FAILOVER [TIMEOUT ms] [FORCE] - Initiate a manual failover
CommandResult HandleFailover(const astra::protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() > 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'FAILOVER' command");
  }

  // Parse optional arguments
  [[maybe_unused]] bool force = false;
  [[maybe_unused]] int64_t timeout_ms = 0;

  for (size_t i = 0; i < command.ArgCount(); ++i) {
    std::string arg = absl::AsciiStrToUpper(command[i].AsString());

    if (arg == "FORCE") {
      force = true;
    } else if (arg == "TIMEOUT") {
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR syntax error");
      }
      if (!absl::SimpleAtoi(command[++i].AsString(), &timeout_ms)) {
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
      }
    }
  }

  // Note: In a real implementation with replication, this would initiate a
  // failover For now, we just return OK
  RespValue result;
  result.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(result);
}

// LATENCY [HELP] - Latency monitoring
CommandResult HandleLatency(const astra::protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() > 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'LATENCY' command");
  }

  if (command.ArgCount() == 0 ||
      absl::AsciiStrToUpper(command[0].AsString()) == "HELP") {
    // LATENCY HELP
    std::string help_text =
        "LATENCY <subcommand> [<arg> [value] ...]. Subcommands are:\n"
        "HELP - Show this help text\n"
        "DOCTOR - Return a different human readable latency analysis report\n"
        "GRAPH - Return a latency graph\n"
        "HISTORY - Return timestamp-latency samples\n"
        "LATEST - Return the latest latency samples\n"
        "RESET - Reset latency data\n"
        "EVENTS - Return latest latency events";
    protocol::RespValue resp;
    resp.SetString(help_text, protocol::RespType::kBulkString);
    return CommandResult(resp);
  }

  // Note: In a real implementation, this would provide latency monitoring data
  // For now, we return empty array
  return CommandResult(RespValue(std::vector<RespValue>()));
}

// MONITOR - Echo all commands executed by the server
CommandResult HandleMonitor(const astra::protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'MONITOR' command");
  }

  // Note: In a real implementation, this would enable command monitoring
  // For now, we just return OK (monitoring would be implemented differently)
  RespValue result;
  result.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(result);
}

// SLOWLOG [HELP | GET | LEN | RESET] - Manage the slow log
CommandResult HandleSlowlog(const astra::protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() > 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SLOWLOG' command");
  }

  if (command.ArgCount() == 0 ||
      absl::AsciiStrToUpper(command[0].AsString()) == "HELP") {
    // SLOWLOG HELP
    std::string help_text =
        "SLOWLOG <subcommand> [<arg>]. Subcommands are:\n"
        "GET [count] - Get slow log entries\n"
        "LEN - Get the number of slow log entries\n"
        "RESET - Reset the slow log\n"
        "HELP - Show this help text";
    protocol::RespValue resp;
    resp.SetString(help_text, protocol::RespType::kBulkString);
    return CommandResult(resp);
  }

  std::string subcommand = absl::AsciiStrToUpper(command[0].AsString());

  if (subcommand == "GET") {
    // SLOWLOG GET [count]
    int64_t count = 10;
    if (command.ArgCount() == 2) {
      if (!absl::SimpleAtoi(command[1].AsString(), &count)) {
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
      }
    }

    // Note: In a real implementation, this would return slow log entries
    // For now, we return empty array
    return CommandResult(RespValue(std::vector<RespValue>()));

  } else if (subcommand == "LEN") {
    // SLOWLOG LEN
    // Note: In a real implementation, this would return the number of slow log
    // entries
    return CommandResult(RespValue(static_cast<int64_t>(0)));

  } else if (subcommand == "RESET") {
    // SLOWLOG RESET
    // Note: In a real implementation, this would reset the slow log
    RespValue result;
    result.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(result);
  }

  // Unknown subcommand
  return CommandResult(false,
                       "ERR unknown SLOWLOG subcommand '" + subcommand + "'");
}

// LOLWUT [version] [columns] [rows] - Display Redis ASCII art
CommandResult HandleLolwut(const protocol::Command& command,
                           CommandContext* context) {
  [[maybe_unused]] Database* db = context->GetDatabase();

  // Parse optional parameters
  int64_t version = 6;
  int64_t columns = 80;
  int64_t rows = 8;

  if (command.ArgCount() > 0) {
    std::string first_arg = command[0].AsString();
    if (first_arg != "LOLWUT") {
      // First argument might be version
      if (!absl::SimpleAtoi(command[0].AsString(), &version)) {
        // If not a number, it's an error
        return CommandResult(false, "ERR invalid version number");
      }
    }
  }

  if (command.ArgCount() > 1) {
    if (!absl::SimpleAtoi(command[1].AsString(), &columns)) {
      return CommandResult(false, "ERR invalid columns number");
    }
  }

  if (command.ArgCount() > 2) {
    if (!absl::SimpleAtoi(command[2].AsString(), &rows)) {
      return CommandResult(false, "ERR invalid rows number");
    }
  }

  // Limit parameters to reasonable values
  version = std::max(static_cast<int64_t>(1),
                     std::min(static_cast<int64_t>(6), version));
  columns = std::max(static_cast<int64_t>(10),
                     std::min(static_cast<int64_t>(600), columns));
  rows = std::max(static_cast<int64_t>(2),
                  std::min(static_cast<int64_t>(50), rows));

  // Generate simple ASCII art (AstraDB logo)
  std::string art = R"(
  _____   _____   _____   _____ 
 /  _  \ /  ___| /  ___| /  _  \
 | | | | | |     | |     | | | |
 | |_| | | |___  | |___  | |_| |
 \_____/ \_____| \_____| \_____/ 
 
  Redis-compatible NoSQL Database
  Version 1.0.0 - High Performance
  
)";

  // Create multi-bulk response
  std::vector<protocol::RespValue> result;

  // Add version info
  std::vector<protocol::RespValue> info;
  info.emplace_back(version);
  info.emplace_back(columns);
  info.emplace_back(rows);
  result.emplace_back(protocol::RespValue(std::move(info)));

  // Add art lines
  std::istringstream iss(art);
  std::string line;
  while (std::getline(iss, line)) {
    result.emplace_back(line);
  }

  protocol::RespValue resp;
  resp.SetArray(std::move(result));
  return CommandResult(resp);
}

// HELLO [protover [AUTH username password] [SETNAME clientname]] - Redis 6.0+
// authentication and protocol negotiation
CommandResult HandleHello(const protocol::Command& command,
                          CommandContext* context) {
  // HELLO command is used for protocol version negotiation and authentication
  // Returns server information and can optionally set authentication

  int64_t protover = 2;  // Default RESP2

  // Parse protocol version if provided
  if (command.ArgCount() > 0) {
    const std::string& protover_str = command[0].AsString();
    if (!absl::SimpleAtoi(protover_str, &protover)) {
      return CommandResult(false, "ERR invalid protocol version");
    }
    if (protover != 2 && protover != 3) {
      return CommandResult(false, "NOPROTO unsupported protocol version");
    }

    // Set the protocol version for this connection
    context->SetProtocolVersion(static_cast<int>(protover));
  }

  // For now, we skip AUTH and SETNAME parsing
  // A full implementation would handle authentication and client name setting

  // Build server information
  // RESP2 (protover=2 or no args): Return array format
  // RESP3 (protover=3): Return map format

  if (protover == 2) {
    // RESP2: Return array format
    std::vector<protocol::RespValue> result;
    result.reserve(14);

    result.push_back(protocol::RespValue(std::string("server")));
    result.push_back(protocol::RespValue(std::string("redis")));
    result.push_back(protocol::RespValue(std::string("version")));
    result.push_back(protocol::RespValue(std::string("7.4.1")));
    result.push_back(protocol::RespValue(std::string("proto")));
    result.push_back(protocol::RespValue(protover));
    result.push_back(protocol::RespValue(std::string("id")));
    result.push_back(
        protocol::RespValue(static_cast<int64_t>(context->GetConnectionId())));
    result.push_back(protocol::RespValue(std::string("mode")));
    result.push_back(protocol::RespValue(std::string("standalone")));
    result.push_back(protocol::RespValue(std::string("role")));
    result.push_back(protocol::RespValue(std::string("master")));
    result.push_back(protocol::RespValue(std::string("modules")));
    result.push_back(protocol::RespValue(
        std::vector<protocol::RespValue>()));  // Empty array

    return CommandResult(protocol::RespValue(std::move(result)));
  } else {
    // RESP3: Return map format
    absl::flat_hash_map<std::string, protocol::RespValue> result;

    result["server"] = protocol::RespValue(std::string("redis"));
    result["version"] = protocol::RespValue(std::string("7.4.1"));
    result["proto"] = protocol::RespValue(protover);
    result["id"] =
        protocol::RespValue(static_cast<int64_t>(context->GetConnectionId()));
    result["mode"] = protocol::RespValue(std::string("standalone"));
    result["role"] = protocol::RespValue(std::string("master"));
    result["modules"] = protocol::RespValue(std::vector<protocol::RespValue>());

    auto resp_value = protocol::RespValue(std::move(result));
    ASTRADB_LOG_DEBUG("HELLO: Returning map with size={}, type={}",
                      result.size(), static_cast<int>(resp_value.GetType()));
    return CommandResult(resp_value);
  }
}

// QUIT - Closes the connection (deprecated in Redis 7.2)
CommandResult HandleQuit(const protocol::Command& command,
                         CommandContext* context) {
  // QUIT command closes the connection
  // Note: This command is deprecated as of Redis 7.2.0
  // Clients should simply close the connection instead

  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// SLAVEOF host port - Configure replication (deprecated, use REPLICAOF)
CommandResult HandleSlaveof(const protocol::Command& command,
                            CommandContext* context) {
  // SLAVEOF is deprecated since Redis 5.0.0
  // It's an alias for REPLICAOF

  if (command.ArgCount() != 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SLAVEOF' command");
  }

  const std::string& host = command[0].AsString();
  const std::string& port = command[1].AsString();

  // For now, we delegate to REPLICAOF
  // Note: In a real implementation, this would configure replication

  // "NO ONE" means stop being a replica
  if (absl::AsciiStrToUpper(host) == "NO" &&
      absl::AsciiStrToUpper(port) == "ONE") {
    protocol::RespValue resp;
    resp.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(resp);
  }

  // Configure this instance to replicate from the specified host:port
  // Note: Full replication implementation would be complex
  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// Auto-register all admin commands
ASTRADB_REGISTER_COMMAND(PING, 1, "readonly,fast", RoutingStrategy::kNone,
                         HandlePing);
ASTRADB_REGISTER_COMMAND(INFO, 1, "readonly", RoutingStrategy::kNone,
                         HandleInfo);
ASTRADB_REGISTER_COMMAND(COMMAND, -1, "readonly,admin", RoutingStrategy::kNone,
                         HandleCommand);
ASTRADB_REGISTER_COMMAND(DEBUG, -2, "admin", RoutingStrategy::kNone,
                         HandleDebug);
ASTRADB_REGISTER_COMMAND(CLUSTER, -2, "readonly", RoutingStrategy::kNone,
                         HandleCluster);
ASTRADB_REGISTER_COMMAND(MIGRATE, -6, "write", RoutingStrategy::kByFirstKey,
                         HandleMigrate);
ASTRADB_REGISTER_COMMAND(MODULE, -2, "admin", RoutingStrategy::kNone,
                         HandleModule);
ASTRADB_REGISTER_COMMAND(CONFIG, -2, "admin,slow,dangerous",
                         RoutingStrategy::kNone, HandleConfig);
ASTRADB_REGISTER_COMMAND(SCAN, -2, "readonly,slow", RoutingStrategy::kNone,
                         HandleScan);
ASTRADB_REGISTER_COMMAND(MEMORY, -2, "readonly,slow", RoutingStrategy::kNone,
                         HandleMemory);
ASTRADB_REGISTER_COMMAND(ASKING, 1, "fast", RoutingStrategy::kNone,
                         HandleAsking);
ASTRADB_REGISTER_COMMAND(BGSAVE, 1, "admin", RoutingStrategy::kNone,
                         HandleBgSave);
ASTRADB_REGISTER_COMMAND(LASTSAVE, 1, "readonly", RoutingStrategy::kNone,
                         HandleLastSave);
ASTRADB_REGISTER_COMMAND(SAVE, 1, "admin", RoutingStrategy::kNone, HandleSave);
ASTRADB_REGISTER_COMMAND(TYPE, 2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleType);
ASTRADB_REGISTER_COMMAND(KEYS, 2, "readonly", RoutingStrategy::kNone,
                         HandleKeys);
ASTRADB_REGISTER_COMMAND(RANDOMKEY, 1, "readonly,fast", RoutingStrategy::kNone,
                         HandleRandomKey);
ASTRADB_REGISTER_COMMAND(RENAME, 3, "write", RoutingStrategy::kByFirstKey,
                         HandleRename);
ASTRADB_REGISTER_COMMAND(RENAMENX, 3, "write", RoutingStrategy::kByFirstKey,
                         HandleRenameNx);
ASTRADB_REGISTER_COMMAND(MOVE, 3, "write", RoutingStrategy::kByFirstKey,
                         HandleMove);
ASTRADB_REGISTER_COMMAND(OBJECT, -2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleObject);
ASTRADB_REGISTER_COMMAND(TOUCH, -2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleTouch);
ASTRADB_REGISTER_COMMAND(DBSIZE, 1, "readonly,fast", RoutingStrategy::kNone,
                         HandleDbSize);
ASTRADB_REGISTER_COMMAND(FLUSHDB, 1, "write", RoutingStrategy::kNone,
                         HandleFlushDb);
ASTRADB_REGISTER_COMMAND(FLUSHALL, 1, "write", RoutingStrategy::kNone,
                         HandleFlushAll);
ASTRADB_REGISTER_COMMAND(TIME, 1, "readonly,fast", RoutingStrategy::kNone,
                         HandleTime);
ASTRADB_REGISTER_COMMAND(SHUTDOWN, -1, "admin", RoutingStrategy::kNone,
                         HandleShutdown);
ASTRADB_REGISTER_COMMAND(SWAPDB, 3, "write", RoutingStrategy::kNone,
                         HandleSwapDb);
ASTRADB_REGISTER_COMMAND(WAIT, 3, "readonly", RoutingStrategy::kNone,
                         HandleWait);
ASTRADB_REGISTER_COMMAND(WAITAOF, 4, "readonly", RoutingStrategy::kNone,
                         HandleWaitAOF);
ASTRADB_REGISTER_COMMAND(READONLY, 1, "fast", RoutingStrategy::kNone,
                         HandleReadonly);
ASTRADB_REGISTER_COMMAND(READWRITE, 1, "fast", RoutingStrategy::kNone,
                         HandleReadwrite);
ASTRADB_REGISTER_COMMAND(RESET, 1, "fast", RoutingStrategy::kNone, HandleReset);
// RESTORE-ASKING is registered manually due to the hyphen in the command name
namespace {
struct CommandRegistrar_RESTORE_ASKING {
  CommandRegistrar_RESTORE_ASKING() {
    ::astra::commands::RuntimeCommandRegistry::Instance().RegisterCommand(
        "RESTORE-ASKING", 1, "fast", RoutingStrategy::kNone,
        HandleRestoreAsking);
  }
} g_cmd_reg_restore_asking;
}  // namespace
ASTRADB_REGISTER_COMMAND(BGREWRITEAOF, 1, "admin", RoutingStrategy::kNone,
                         HandleBgRewriteAof);
ASTRADB_REGISTER_COMMAND(FAILOVER, -1, "admin", RoutingStrategy::kNone,
                         HandleFailover);
ASTRADB_REGISTER_COMMAND(LATENCY, -2, "readonly", RoutingStrategy::kNone,
                         HandleLatency);
ASTRADB_REGISTER_COMMAND(MONITOR, 1, "admin", RoutingStrategy::kNone,
                         HandleMonitor);
ASTRADB_REGISTER_COMMAND(SLOWLOG, -2, "readonly", RoutingStrategy::kNone,
                         HandleSlowlog);
ASTRADB_REGISTER_COMMAND(LOLWUT, -1, "readonly", RoutingStrategy::kNone,
                         HandleLolwut);
ASTRADB_REGISTER_COMMAND(SELECT, 2, "fast", RoutingStrategy::kNone,
                         HandleSelect);
ASTRADB_REGISTER_COMMAND(AUTH, -2, "no-auth", RoutingStrategy::kNone,
                         HandleAuth);
ASTRADB_REGISTER_COMMAND(ACL, -2, "admin", RoutingStrategy::kNone, HandleAcl);
ASTRADB_REGISTER_COMMAND(HELLO, -1, "readonly,fast", RoutingStrategy::kNone,
                         HandleHello);
ASTRADB_REGISTER_COMMAND(QUIT, 1, "readonly,fast", RoutingStrategy::kNone,
                         HandleQuit);
ASTRADB_REGISTER_COMMAND(SLAVEOF, 3, "write,admin,no-script",
                         RoutingStrategy::kNone, HandleSlaveof);

}  // namespace astra::commands
