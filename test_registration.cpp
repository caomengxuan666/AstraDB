// ==============================================================================
// Test Command Registration
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "src/astra/commands/command_auto_register.hpp"
#include "src/astra/commands/string_commands.hpp"
#include "src/astra/commands/hash_commands.hpp"
#include "src/astra/commands/set_commands.hpp"
#include "src/astra/commands/zset_commands.hpp"
#include "src/astra/commands/list_commands.hpp"
#include "src/astra/commands/admin_commands.hpp"
#include "src/astra/commands/ttl_commands.hpp"
#include "src/astra/commands/script_commands.hpp"
#include "src/astra/commands/command_handler.hpp"
#include <iostream>

using namespace astra::commands;

// Force linker to include all command object files
extern void* __attribute__((weak)) g_cmd_reg_GET;
extern void* __attribute__((weak)) g_cmd_reg_SET;
extern void* __attribute__((weak)) g_cmd_reg_DEL;
extern void* __attribute__((weak)) g_cmd_reg_MGET;
extern void* __attribute__((weak)) g_cmd_reg_MSET;
extern void* __attribute__((weak)) g_cmd_reg_EXISTS;

extern void* __attribute__((weak)) g_cmd_reg_HSET;
extern void* __attribute__((weak)) g_cmd_reg_HGET;
extern void* __attribute__((weak)) g_cmd_reg_HDEL;
extern void* __attribute__((weak)) g_cmd_reg_HEXISTS;
extern void* __attribute__((weak)) g_cmd_reg_HGETALL;
extern void* __attribute__((weak)) g_cmd_reg_HLEN;

extern void* __attribute__((weak)) g_cmd_reg_SADD;
extern void* __attribute__((weak)) g_cmd_reg_SREM;
extern void* __attribute__((weak)) g_cmd_reg_SMEMBERS;
extern void* __attribute__((weak)) g_cmd_reg_SISMEMBER;
extern void* __attribute__((weak)) g_cmd_reg_SCARD;

extern void* __attribute__((weak)) g_cmd_reg_ZADD;
extern void* __attribute__((weak)) g_cmd_reg_ZRANGE;
extern void* __attribute__((weak)) g_cmd_reg_ZREM;
extern void* __attribute__((weak)) g_cmd_reg_ZSCORE;
extern void* __attribute__((weak)) g_cmd_reg_ZCARD;
extern void* __attribute__((weak)) g_cmd_reg_ZCOUNT;

extern void* __attribute__((weak)) g_cmd_reg_LPUSH;
extern void* __attribute__((weak)) g_cmd_reg_RPUSH;
extern void* __attribute__((weak)) g_cmd_reg_LPOP;
extern void* __attribute__((weak)) g_cmd_reg_RPOP;
extern void* __attribute__((weak)) g_cmd_reg_LLEN;
extern void* __attribute__((weak)) g_cmd_reg_LINDEX;
extern void* __attribute__((weak)) g_cmd_reg_LSET;
extern void* __attribute__((weak)) g_cmd_reg_LRANGE;
extern void* __attribute__((weak)) g_cmd_reg_LTRIM;
extern void* __attribute__((weak)) g_cmd_reg_LREM;
extern void* __attribute__((weak)) g_cmd_reg_LINSERT;
extern void* __attribute__((weak)) g_cmd_reg_RPOPLPUSH;

extern void* __attribute__((weak)) g_cmd_reg_PING;
extern void* __attribute__((weak)) g_cmd_reg_INFO;

extern void* __attribute__((weak)) g_cmd_reg_EXPIRE;
extern void* __attribute__((weak)) g_cmd_reg_EXPIREAT;
extern void* __attribute__((weak)) g_cmd_reg_PEXPIRE;
extern void* __attribute__((weak)) g_cmd_reg_PEXPIREAT;
extern void* __attribute__((weak)) g_cmd_reg_TTL;
extern void* __attribute__((weak)) g_cmd_reg_PTTL;
extern void* __attribute__((weak)) g_cmd_reg_PERSIST;

extern void* __attribute__((weak)) g_cmd_reg_EVAL;
extern void* __attribute__((weak)) g_cmd_reg_EVALSHA;
extern void* __attribute__((weak)) g_cmd_reg_SCRIPT;

void* g_force_link[] = {
  &g_cmd_reg_GET, &g_cmd_reg_SET, &g_cmd_reg_DEL, &g_cmd_reg_MGET, &g_cmd_reg_MSET, &g_cmd_reg_EXISTS,
  &g_cmd_reg_HSET, &g_cmd_reg_HGET, &g_cmd_reg_HDEL, &g_cmd_reg_HEXISTS, &g_cmd_reg_HGETALL, &g_cmd_reg_HLEN,
  &g_cmd_reg_SADD, &g_cmd_reg_SREM, &g_cmd_reg_SMEMBERS, &g_cmd_reg_SISMEMBER, &g_cmd_reg_SCARD,
  &g_cmd_reg_ZADD, &g_cmd_reg_ZRANGE, &g_cmd_reg_ZREM, &g_cmd_reg_ZSCORE, &g_cmd_reg_ZCARD, &g_cmd_reg_ZCOUNT,
  &g_cmd_reg_LPUSH, &g_cmd_reg_RPUSH, &g_cmd_reg_LPOP, &g_cmd_reg_RPOP, &g_cmd_reg_LLEN, &g_cmd_reg_LINDEX,
  &g_cmd_reg_LSET, &g_cmd_reg_LRANGE, &g_cmd_reg_LTRIM, &g_cmd_reg_LREM, &g_cmd_reg_LINSERT, &g_cmd_reg_RPOPLPUSH,
  &g_cmd_reg_PING, &g_cmd_reg_INFO,
  &g_cmd_reg_EXPIRE, &g_cmd_reg_EXPIREAT, &g_cmd_reg_PEXPIRE, &g_cmd_reg_PEXPIREAT, &g_cmd_reg_TTL, &g_cmd_reg_PTTL, &g_cmd_reg_PERSIST,
  &g_cmd_reg_EVAL, &g_cmd_reg_EVALSHA, &g_cmd_reg_SCRIPT
};

int main() {
  std::cout << "Command count before ApplyToRegistry: "
            << RuntimeCommandRegistry::Instance().GetCommandCount() << std::endl;

  CommandRegistry registry;
  RuntimeCommandRegistry::Instance().ApplyToRegistry(registry);

  auto names = registry.GetCommandNames();
  std::cout << "Command count after ApplyToRegistry: " << names.size() << std::endl;

  for (const auto& name : names) {
    std::cout << "  - " << name << std::endl;
  }

  return 0;
}