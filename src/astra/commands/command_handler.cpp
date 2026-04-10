// ==============================================================================
// Command Handler Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "command_handler.hpp"

#include <absl/strings/ascii.h>

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace astra::commands {

void CommandRegistry::Register(const CommandInfo& info,
                               CommandHandler handler) noexcept {
  CommandEntry entry;
  entry.info = info;
  entry.handler = std::move(handler);
  // Store command name in uppercase for case-insensitive lookup
  std::string upper_name = absl::AsciiStrToUpper(info.name);
  commands_.insert_or_assign(upper_name, std::move(entry));
}

void CommandRegistry::Unregister(const std::string& name) noexcept {
  std::string upper_name = absl::AsciiStrToUpper(name);
  commands_.erase(upper_name);
}

bool CommandRegistry::Exists(const std::string& name) const noexcept {
  std::string upper_name = absl::AsciiStrToUpper(name);
  return commands_.find(upper_name) != commands_.end();
}

const CommandInfo* CommandRegistry::GetInfo(
    const std::string& name) const noexcept {
  std::string upper_name = absl::AsciiStrToUpper(name);
  ASTRADB_LOG_TRACE("GetInfo: name='{}', upper_name='{}'", name, upper_name);
  auto it = commands_.find(upper_name);
  if (it != commands_.end()) {
    ASTRADB_LOG_TRACE("GetInfo: found command '{}'", name);
    return &it->second.info;
  }
  ASTRADB_LOG_TRACE("GetInfo: command '{}' not found", name);
  return nullptr;
}

CommandResult CommandRegistry::Execute(const astra::protocol::Command& command,
                                       CommandContext* context) const noexcept {
  if (command.name.empty()) {
    return CommandResult(false, "ERR empty command name");
  }

  // Convert command name to uppercase for case-insensitive lookup
  std::string upper_name = absl::AsciiStrToUpper(command.name);

  auto it = commands_.find(upper_name);
  if (it == commands_.end()) {
    return CommandResult(false, "ERR unknown command '" + command.name + "'");
  }

  const auto& entry = it->second;

  // Check arity
  // Redis arity includes command name, but ArgCount() returns only arguments
  // So we compare: ArgCount() + 1 (for command name) vs arity
  // arity = 0 means unlimited arguments (no check needed)
  // arity > 0 means exact number of total args (command + arguments)
  // arity < 0 means at least |arity| total args
  int total_args = static_cast<int>(command.ArgCount()) + 1;

  if (entry.info.arity > 0) {
    // Fixed arity - exact number required
    if (total_args != entry.info.arity) {
      std::ostringstream oss;
      oss << "ERR wrong number of arguments for '" << upper_name << "' command";
      return CommandResult(false, oss.str());
    }
  } else if (entry.info.arity < 0) {
    // Variable arity - at least |arity| args required
    int min_args = -entry.info.arity;
    if (total_args < min_args) {
      std::ostringstream oss;
      oss << "ERR wrong number of arguments for '" << upper_name << "' command";
      return CommandResult(false, oss.str());
    }
  }
  // arity == 0 means unlimited, no check needed

  // ========== Permission Checking ==========
  // Only check permissions if ACL is enabled and ACL manager is available
  if (context->GetAclManager()) {
    // Check command permission
    if (!context->CheckCommandPermission(command.name)) {
      return CommandResult(false, "NOPERM No permissions to execute command '" +
                                      command.name + "'");
    }

    // Check key permission for commands that access keys
    // Skip key permission check for commands that don't access keys
    static const absl::flat_hash_set<std::string> non_key_commands = {
        "AUTH",      "PING",        "ECHO",       "QUIT",         "SELECT",
        "HELLO",     "CLIENT",      "INFO",       "TIME",         "MONITOR",
        "DEBUG",     "SHUTDOWN",    "SAVE",       "BGSAVE",       "LASTSAVE",
        "DBSIZE",    "KEYS",        "RANDOMKEY",  "SCAN",         "TYPE",
        "EXISTS",    "TTL",         "PTTL",       "EXPIRE",       "PEXPIRE",
        "EXPIREAT",  "PEXPIREAT",   "PERSIST",    "SORT",         "ACL",
        "SUBSCRIBE", "UNSUBSCRIBE", "PSUBSCRIBE", "PUNSUBSCRIBE", "PUBLISH",
        "PUBSUB",    "MULTI",       "EXEC",       "DISCARD",      "WATCH",
        "UNWATCH"};

    // Only check key permission if command is not in the non-key command list
    if (non_key_commands.find(upper_name) == non_key_commands.end() &&
        command.ArgCount() > 0 &&
        (command[0].IsBulkString() || command[0].IsSimpleString())) {
      const std::string& key = command[0].AsString();
      if (!context->CheckKeyPermission(key)) {
        return CommandResult(
            false, "NOPERM No permissions to access key '" + key + "'");
      }
    }
  }

  // ========== Command Parameter Caching ==========
  // Generate cache key from command name and arguments
  if (caching_enabled_ && !entry.info.is_write) {
    // Only cache read commands
    std::string cache_key = GenerateCacheKey(command);

    // Check cache
    auto cache_it = command_cache_.find(cache_key);
    if (cache_it != command_cache_.end()) {
      // Cache hit
      cache_hit_count_++;
      cache_it->second.last_accessed_ms = absl::GetCurrentTimeNanos() / 1000000;
      cache_it->second.access_count++;

      // Return cached result (if available)
      // Note: In a real implementation, we'd need to store the actual result
      // For now, we just track cache hits
    } else {
      // Cache miss
      cache_miss_count_++;

      // Add to cache
      if (command_cache_.size() >= kMaxCacheSize) {
        // LRU eviction
        EvictLRUCacheEntry();
      }

      // Create cache entry
      CachedCommandEntry cache_entry;
      cache_entry.command_name = upper_name;
      cache_entry.hash = cache_key;
      cache_entry.created_at_ms = absl::GetCurrentTimeNanos() / 1000000;
      cache_entry.last_accessed_ms = cache_entry.created_at_ms;
      cache_entry.access_count = 1;
      cache_entry.is_read_only = true;

      // Cache command parameters
      for (size_t i = 0; i < command.ArgCount(); ++i) {
        const auto& arg = command[i];
        std::string arg_str;
        if (arg.IsBulkString() || arg.IsSimpleString()) {
          arg_str = arg.AsString();
        } else if (arg.IsInteger()) {
          arg_str = std::to_string(arg.AsInteger());
        } else {
          arg_str = "";
        }
        CachedParameterValue param_value;
        param_value.type = CachedParameterValue::Type::kString;
        param_value.string_value = arg_str;
        cache_entry.parameters.emplace_back("arg_" + std::to_string(i),
                                            param_value);
      }

      cache_entry.param_count =
          static_cast<uint32_t>(cache_entry.parameters.size());
      cache_entry.estimated_size_bytes =
          static_cast<uint32_t>(upper_name.size() + cache_key.size() +
                                cache_entry.parameters.size() * 64);

      command_cache_[cache_key] = std::move(cache_entry);
    }
  }

  // Execute handler
  try {
    return entry.handler(command, context);
  } catch (const std::exception& e) {
    return CommandResult(false, std::string("ERR ") + e.what());
  } catch (...) {
    return CommandResult(false, "ERR unknown error");
  }
}

std::vector<std::string> CommandRegistry::GetCommandNames() const noexcept {
  ASTRADB_LOG_TRACE("GetCommandNames: commands_.size()={}", commands_.size());
  std::vector<std::string> names;
  names.reserve(commands_.size());
  for (const auto& pair : commands_) {
    names.push_back(pair.first);
  }
  std::sort(names.begin(), names.end());
  ASTRADB_LOG_TRACE("GetCommandNames: returning {} names", names.size());
  return names;
}

RoutingStrategy CommandRegistry::GetRoutingStrategy(
    const std::string& command_name) const noexcept {
  std::string upper_name = absl::AsciiStrToUpper(command_name);
  auto it = commands_.find(upper_name);
  if (it != commands_.end()) {
    return it->second.info.routing;
  }
  return RoutingStrategy::kNone;
}

// Generate cache key from command name and arguments
std::string CommandRegistry::GenerateCacheKey(
    const astra::protocol::Command& command) const noexcept {
  std::string upper_name = absl::AsciiStrToUpper(command.name);

  std::vector<std::pair<std::string, CachedParameterValue>> params;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    std::string arg_str;
    if (arg.IsBulkString() || arg.IsSimpleString()) {
      arg_str = arg.AsString();
    } else if (arg.IsInteger()) {
      arg_str = std::to_string(arg.AsInteger());
    } else {
      arg_str = "";
    }
    CachedParameterValue param_value;
    param_value.type = CachedParameterValue::Type::kString;
    param_value.string_value = arg_str;
    params.emplace_back("arg_" + std::to_string(i), param_value);
  }

  return CommandCacheFlatbuffersSerializer::GenerateCommandHash(upper_name,
                                                                params);
}

// Evict LRU cache entry
void CommandRegistry::EvictLRUCacheEntry() const noexcept {
  if (command_cache_.empty()) {
    return;
  }

  // Find least recently used entry
  auto lru_it = command_cache_.begin();
  for (auto it = command_cache_.begin(); it != command_cache_.end(); ++it) {
    if (it->second.last_accessed_ms < lru_it->second.last_accessed_ms) {
      lru_it = it;
    }
  }

  // Erase the LRU entry
  command_cache_.erase(lru_it);
  cache_eviction_count_++;
}
}  // namespace astra::commands
