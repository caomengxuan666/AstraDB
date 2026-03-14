// ==============================================================================
// Script Commands Implementation (Lua Scripting)
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/container/flat_hash_map.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "command_handler.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace astra::commands {

// Lua script context
class LuaScriptContext {
 public:
  LuaScriptContext(Database* db, CommandRegistry* registry, CommandContext* context);
  ~LuaScriptContext();

  // Execute Lua script
  CommandResult Execute(const std::string& script,
                        const std::vector<std::string>& keys,
                        const std::vector<std::string>& args);

  lua_State* GetLuaState() { return lua_state_; }
  Database* GetDatabase() { return db_; }

 private:
  // Lua C functions
  static int LuaCall(lua_State* L);
  static int LuaPcall(lua_State* L);

  // Helper functions
  static void PushRespValueToLua(lua_State* L, const RespValue& value);
  static bool CheckCommandBlacklist(const std::string& cmd_name);

  // Members
  Database* db_;
  CommandRegistry* command_registry_;
  CommandContext* command_context_;
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
CommandResult HandleEval(const astra::protocol::Command& command,
                         CommandContext* context);
CommandResult HandleEvalSha(const astra::protocol::Command& command,
                            CommandContext* context);
CommandResult HandleScript(const astra::protocol::Command& command,
                           CommandContext* context);

// Script commands are auto-registered via ASTRADB_REGISTER_COMMAND macro

}  // namespace astra::commands
