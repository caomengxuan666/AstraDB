// ==============================================================================
// Script Commands Implementation (Lua Scripting)
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/container/flat_hash_map.h>
#include "command_handler.hpp"
#include <string>
#include <memory>
#include <unordered_map>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace astra::commands {

// Lua script context
class LuaScriptContext {
 public:
  LuaScriptContext(Database* db);
  ~LuaScriptContext();
  
  // Execute Lua script
  CommandResult Execute(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args);
  
  lua_State* GetLuaState() { return lua_state_; }
  Database* GetDatabase() { return db_; }
  
 private:
  static int LuaCall(lua_State* L);
  static int LuaPcall(lua_State* L);
  
  Database* db_;
  lua_State* lua_state_;
};

// Script cache (SHA1 -> script)
class ScriptCache {
 public:
  ScriptCache() = default;
  
  // Cache script
  void Cache(const std::string& sha1, const std::string& script);
  
  // Get script by SHA1
  std::optional<std::string> Get(const std::string& sha1) const;
  
  // Check if script exists
  bool Exists(const std::string& sha1) const;
  
  // Clear cache
  void Clear();
  
  // Get all SHA1 hashes
  std::vector<std::string> GetAllHashes() const;
  
 private:
  absl::flat_hash_map<std::string, std::string> cache_;
};

// Global script functions
ScriptCache& GetGlobalScriptCache();

// Command handlers
CommandResult HandleEval(const astra::protocol::Command& command, CommandContext* context);
CommandResult HandleEvalSha(const astra::protocol::Command& command, CommandContext* context);
CommandResult HandleScript(const astra::protocol::Command& command, CommandContext* context);

// Script commands are auto-registered via ASTRADB_REGISTER_COMMAND macro

}  // namespace astra::commands