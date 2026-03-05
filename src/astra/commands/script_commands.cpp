// ==============================================================================
// Script Commands Implementation (Lua Scripting)
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "script_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/protocol/resp/resp_builder.hpp"
#include "astra/base/logging.hpp"
#include <sha1.hpp>
#include <sstream>
#include <mutex>

namespace astra::commands {

// SHA1 hash function using vog/sha1 library
static std::string SHA1(const std::string& input) {
  ::SHA1 sha1;
  sha1.update(input);
  return sha1.final();
}

// Lua script context implementation
LuaScriptContext::LuaScriptContext(Database* db) : db_(db) {
  lua_state_ = luaL_newstate();
  luaL_openlibs(lua_state_);
  
  // Register Redis functions
  lua_register(lua_state_, "call", LuaCall);
  lua_register(lua_state_, "pcall", LuaPcall);
}

LuaScriptContext::~LuaScriptContext() {
  if (lua_state_) {
    lua_close(lua_state_);
  }
}

int LuaScriptContext::LuaCall(lua_State* L) {
  // Get context from registry
  lua_getfield(L, LUA_REGISTRYINDEX, "astra_context");
  LuaScriptContext* ctx = static_cast<LuaScriptContext*>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  
  if (!ctx) {
    lua_pushnil(L);
    lua_pushstring(L, "ERR context not available");
    return 2;
  }
  
  // Get command name
  if (lua_gettop(L) < 1) {
    lua_pushnil(L);
    lua_pushstring(L, "ERR wrong number of arguments");
    return 2;
  }
  
  const char* cmd_name = lua_tostring(L, 1);
  if (!cmd_name) {
    lua_pushnil(L);
    lua_pushstring(L, "ERR command name must be a string");
    return 2;
  }
  
  // Collect arguments
  std::vector<std::string> args;
  for (int i = 2; i <= lua_gettop(L); ++i) {
    if (lua_isstring(L, i)) {
      args.push_back(lua_tostring(L, i));
    } else if (lua_isnumber(L, i)) {
      args.push_back(absl::StrCat(lua_tonumber(L, i)));
    } else if (lua_isboolean(L, i)) {
      args.push_back(lua_toboolean(L, i) ? "1" : "0");
    } else {
      args.push_back("");  // nil -> empty string
    }
  }
  
  // Execute command (simplified - for now just return OK)
  // In a full implementation, we would route through the command registry
  lua_pushstring(L, "OK");
  return 1;
}

int LuaScriptContext::LuaPcall(lua_State* L) {
  // Simplified implementation
  lua_pushstring(L, "OK");
  return 1;
}

CommandResult LuaScriptContext::Execute(const std::string& script, 
                                       const std::vector<std::string>& keys,
                                       const std::vector<std::string>& args) {
  // Store context in registry
  lua_pushlightuserdata(lua_state_, this);
  lua_setfield(lua_state_, LUA_REGISTRYINDEX, "astra_context");
  
  // Set up KEYS and ARGV tables
  lua_newtable(lua_state_);  // KEYS
  for (size_t i = 0; i < keys.size(); ++i) {
    lua_pushstring(lua_state_, keys[i].c_str());
    lua_rawseti(lua_state_, -2, static_cast<int>(i + 1));
  }
  lua_setglobal(lua_state_, "KEYS");
  
  lua_newtable(lua_state_);  // ARGV
  for (size_t i = 0; i < args.size(); ++i) {
    lua_pushstring(lua_state_, args[i].c_str());
    lua_rawseti(lua_state_, -2, static_cast<int>(i + 1));
  }
  lua_setglobal(lua_state_, "ARGV");
  
  // Load and execute script
  int load_result = luaL_loadstring(lua_state_, script.c_str());
  if (load_result != LUA_OK) {
    std::string error = "ERR " + std::string(lua_tostring(lua_state_, -1));
    lua_pop(lua_state_, 1);
    return CommandResult(false, error);
  }
  
  int call_result = lua_pcall(lua_state_, 0, LUA_MULTRET, 0);
  if (call_result != LUA_OK) {
    std::string error = "ERR " + std::string(lua_tostring(lua_state_, -1));
    lua_pop(lua_state_, 1);
    return CommandResult(false, error);
  }
  
  // Get return values
  int num_returns = lua_gettop(lua_state_);
  if (num_returns == 0) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }
  
  // Check if Lua returned a table (array)
  if (num_returns == 1 && lua_istable(lua_state_, 1)) {
    // Handle table return - convert Lua array to RESP array
    std::vector<RespValue> results;
    
    // Get the length of the Lua table
    int len = lua_objlen(lua_state_, 1);
    
    // Iterate through the table using numeric indices
    for (int i = 1; i <= len; ++i) {
      lua_rawgeti(lua_state_, 1, i);  // Push table[i] onto stack
      if (lua_isnil(lua_state_, -1)) {
        results.push_back(RespValue(RespType::kNullBulkString));
      } else if (lua_isboolean(lua_state_, -1)) {
        results.push_back(RespValue(static_cast<int64_t>(lua_toboolean(lua_state_, -1) ? 1 : 0)));
      } else if (lua_isnumber(lua_state_, -1)) {
        double num = lua_tonumber(lua_state_, -1);
        if (num == static_cast<int64_t>(num)) {
          results.push_back(RespValue(static_cast<int64_t>(num)));
        } else {
          results.push_back(RespValue(absl::StrCat(num)));
        }
      } else if (lua_isstring(lua_state_, -1)) {
        const char* str = lua_tostring(lua_state_, -1);
        std::string str_value(str ? str : "");
        results.push_back(RespValue(str_value));
      } else {
        results.push_back(RespValue("(nil)"));
      }
      lua_pop(lua_state_, 1);  // Pop the value
    }
    
    lua_pop(lua_state_, 1);  // Pop the table
    
    if (results.empty()) {
      return CommandResult(RespValue(std::vector<RespValue>()));
    } else if (results.size() == 1) {
      return CommandResult(std::move(results[0]));
    } else {
      return CommandResult(RespValue(std::move(results)));
    }
  }
  
  // Handle multiple return values
  std::vector<RespValue> results;
  for (int i = 1; i <= num_returns; ++i) {
    if (lua_isnil(lua_state_, i)) {
      results.push_back(RespValue(RespType::kNullBulkString));
    } else if (lua_isboolean(lua_state_, i)) {
      results.push_back(RespValue(static_cast<int64_t>(lua_toboolean(lua_state_, i) ? 1 : 0)));
    } else if (lua_isnumber(lua_state_, i)) {
      double num = lua_tonumber(lua_state_, i);
      if (num == static_cast<int64_t>(num)) {
        results.push_back(RespValue(static_cast<int64_t>(num)));
      } else {
        results.push_back(RespValue(absl::StrCat(num)));
      }
    } else if (lua_isstring(lua_state_, i)) {
      const char* str = lua_tostring(lua_state_, i);
      std::string str_value(str ? str : "");
      results.push_back(RespValue(str_value));
    } else {
      results.push_back(RespValue("(nil)"));
    }
  }
  
  lua_pop(lua_state_, num_returns);
  
  if (results.size() == 1) {
    return CommandResult(std::move(results[0]));
  } else {
    return CommandResult(RespValue(std::move(results)));
  }
}

// Script cache implementation
void ScriptCache::Cache(const std::string& sha1, const std::string& script) {
  cache_[sha1] = script;
}

std::optional<std::string> ScriptCache::Get(const std::string& sha1) const {
  auto it = cache_.find(sha1);
  if (it != cache_.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool ScriptCache::Exists(const std::string& sha1) const {
  return cache_.find(sha1) != cache_.end();
}

void ScriptCache::Clear() {
  cache_.clear();
}

std::vector<std::string> ScriptCache::GetAllHashes() const {
  std::vector<std::string> hashes;
  hashes.reserve(cache_.size());
  for (const auto& pair : cache_) {
    hashes.push_back(pair.first);
  }
  return hashes;
}

// Global script cache
ScriptCache& GetGlobalScriptCache() {
  static ScriptCache cache;
  return cache;
}

// EVAL script numkeys key [key ...] arg [arg ...]
CommandResult HandleEval(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'EVAL' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& script_arg = command[0];
  const auto& numkeys_arg = command[1];

  if (!script_arg.IsBulkString()) {
    return CommandResult(false, "ERR script must be a string");
  }

  std::string script = script_arg.AsString();
  if (!numkeys_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of numkeys argument");
  }
  std::string numkeys_str = numkeys_arg.AsString();
  int numkeys = 0;
  try {
    if (!absl::SimpleAtoi(numkeys_str, &numkeys)) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  if (numkeys < 0) {
    return CommandResult(false, "ERR Number of keys can't be negative");
  }

  if (static_cast<size_t>(numkeys) + 2 > command.ArgCount()) {
    return CommandResult(false, "ERR Number of keys can't be greater than number of args");
  }

  // Extract keys and args
  std::vector<std::string> keys;
  std::vector<std::string> args;
  
  for (int i = 0; i < numkeys; ++i) {
    const auto& arg = command[2 + i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR key must be a string");
    }
    keys.push_back(arg.AsString());
  }
  
  for (size_t i = 2 + numkeys; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR arg must be a string");
    }
    args.push_back(arg.AsString());
  }
  
  // Cache script
  std::string sha1 = SHA1(script);
  GetGlobalScriptCache().Cache(sha1, script);
  
  // Execute script
  LuaScriptContext script_ctx(db);
  return script_ctx.Execute(script, keys, args);
}

// EVALSHA sha1 numkeys key [key ...] arg [arg ...]
CommandResult HandleEvalSha(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'EVALSHA' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& sha1_arg = command[0];
  const auto& numkeys_arg = command[1];

  if (!sha1_arg.IsBulkString()) {
    return CommandResult(false, "ERR sha1 must be a string");
  }

  if (!numkeys_arg.IsBulkString()) {
    return CommandResult(false, "ERR numkeys must be a string");
  }

  std::string sha1 = sha1_arg.AsString();
  std::string numkeys_str = numkeys_arg.AsString();
  
  // Check if script exists in cache
  auto script = GetGlobalScriptCache().Get(sha1);
  if (!script) {
    return CommandResult(false, "NOSCRIPT No matching script. Please use EVAL.");
  }

  int numkeys = 0;
  try {
    if (!absl::SimpleAtoi(numkeys_str, &numkeys)) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  } catch (...) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  if (numkeys < 0) {
    return CommandResult(false, "ERR Number of keys can't be negative");
  }

  if (static_cast<size_t>(numkeys) + 2 > command.ArgCount()) {
    return CommandResult(false, "ERR Number of keys can't be greater than number of args");
  }

  // Extract keys and args
  std::vector<std::string> keys;
  std::vector<std::string> args;
  
  for (int i = 0; i < numkeys; ++i) {
    const auto& arg = command[2 + i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR key must be a string");
    }
    keys.push_back(arg.AsString());
  }
  
  for (size_t i = 2 + numkeys; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR arg must be a string");
    }
    args.push_back(arg.AsString());
  }
  
  // Execute script
  LuaScriptContext script_ctx(db);
  return script_ctx.Execute(*script, keys, args);
}

// SCRIPT subcommand [arg ...]
CommandResult HandleScript(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'SCRIPT' command");
  }

  const auto& subcommand_arg = command[0];
  if (!subcommand_arg.IsBulkString()) {
    return CommandResult(false, "ERR subcommand must be a string");
  }

  std::string subcommand = subcommand_arg.AsString();

  if (subcommand == "FLUSH") {
    // SCRIPT FLUSH [ASYNC|SYNC]
    if (command.ArgCount() > 1) {
      const auto& mode_arg = command[1];
      if (mode_arg.IsBulkString()) {
        std::string mode = mode_arg.AsString();
        if (mode != "ASYNC" && mode != "SYNC") {
          return CommandResult(false, "ERR SCRIPT FLUSH only supports SYNC|ASYNC");
        }
      }
    }
    GetGlobalScriptCache().Clear();
    RespValue flush_resp;
    flush_resp.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(flush_resp);
  } else if (subcommand == "EXISTS") {
    // SCRIPT EXISTS sha1 [sha1 ...]
    std::vector<RespValue> results;
    for (size_t i = 1; i < command.ArgCount(); ++i) {
      const auto& sha1_arg = command[i];
      if (!sha1_arg.IsBulkString()) {
        return CommandResult(false, "ERR sha1 must be a string");
      }
      std::string sha1 = sha1_arg.AsString();
      bool exists = GetGlobalScriptCache().Exists(sha1);
      results.push_back(RespValue(static_cast<int64_t>(exists ? 1 : 0)));
    }
    return CommandResult(RespValue(std::move(results)));
  } else if (subcommand == "LOAD") {
    // SCRIPT LOAD script
    if (command.ArgCount() != 2) {
      return CommandResult(false, "ERR wrong number of arguments for 'SCRIPT LOAD'");
    }
    
    const auto& script_arg = command[1];
    if (!script_arg.IsBulkString()) {
      return CommandResult(false, "ERR script must be a string");
    }
    
    std::string script = script_arg.AsString();
    std::string sha1 = SHA1(script);
    GetGlobalScriptCache().Cache(sha1, script);
    return CommandResult(RespValue(sha1));
  } else {
    return CommandResult(false, "ERR unknown SCRIPT subcommand '" + subcommand + "'");
  }
}

// Auto-register all script commands
ASTRADB_REGISTER_COMMAND(EVAL, -3, "write", RoutingStrategy::kByFirstKey, HandleEval);
ASTRADB_REGISTER_COMMAND(EVALSHA, -3, "write", RoutingStrategy::kByFirstKey, HandleEvalSha);
ASTRADB_REGISTER_COMMAND(SCRIPT, -2, "write", RoutingStrategy::kNone, HandleScript);

}  // namespace astra::commands