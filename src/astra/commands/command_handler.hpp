// ==============================================================================
// Command Handler Interface
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/strings/string_view.h>
#include <absl/types/span.h>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include "astra/protocol/resp/resp_types.hpp"
#include "database.hpp"

// Forward declarations for optional types
namespace astra::cluster {
class GossipManager;
class ShardManager;
struct AstraNodeView;
}

namespace astra::persistence {
class LevelDBAdapter;
class AofWriter;
}

// AOF callback type for logging write commands (zero-copy with absl::Span)
using AofCallback = std::function<void(absl::string_view command, absl::Span<const absl::string_view> args)>;

namespace astra::commands {

using astra::protocol::RespValue;
using astra::protocol::Command;
using astra::protocol::RespType;

// Command context - contains database and connection state
class CommandContext {
 public:
  virtual ~CommandContext() = default;

  // Get current database
  virtual Database* GetDatabase() const = 0;

  // Get current database index
  virtual int GetDBIndex() const = 0;

  // Set current database index
  virtual void SetDBIndex(int index) = 0;

  // Check if authenticated
  virtual bool IsAuthenticated() const = 0;

  // Set authentication status
  virtual void SetAuthenticated(bool auth) = 0;
  
  // Cluster operations (optional - return nullptr/false if not available)
  virtual bool IsClusterEnabled() const { return false; }
  virtual bool ClusterMeet(const std::string& ip, int port) { return false; }
  virtual cluster::GossipManager* GetGossipManager() const { return nullptr; }
  virtual cluster::GossipManager* GetGossipManagerMutable() { return nullptr; }
  virtual cluster::ShardManager* GetClusterShardManager() const { return nullptr; }
  
  // Persistence operations (optional - return nullptr/false if not available)
  virtual bool IsPersistenceEnabled() const { return false; }
  virtual persistence::LevelDBAdapter* GetPersistence() const { return nullptr; }
  
  // AOF logging callback (zero-copy)
  virtual void SetAofCallback(AofCallback callback) { aof_callback_ = std::move(callback); }
  virtual void LogToAof(absl::string_view command, absl::Span<const absl::string_view> args) {
    if (aof_callback_) {
      aof_callback_(command, args);
    }
  }
  
 protected:
  AofCallback aof_callback_;
};

// Command result
struct CommandResult {
  RespValue response;
  bool success = true;
  std::string error;

  CommandResult() = default;
  explicit CommandResult(RespValue resp) : response(std::move(resp)) {}
  CommandResult(bool ok, std::string err) : success(ok), error(std::move(err)) {}
};

// Command handler function signature
using CommandHandler = std::function<CommandResult(
    const astra::protocol::Command& command, CommandContext* context)>;

// Command routing strategy
enum class RoutingStrategy {
  kNone,           // No routing (use default shard)
  kByFirstKey,     // Route based on first argument (key)
  kByArgument,     // Route based on specific argument index
  kAllShards,      // Broadcast to all shards (not implemented yet)
};

// Command metadata
struct CommandInfo {
  std::string name;
  int arity;  // Number of arguments. Positive for fixed, negative for variable (e.g., -2 means at least 1)
  std::string flags;  // Command flags (readonly, fast, etc.)
  RoutingStrategy routing;  // How to route this command
  
  CommandInfo() : routing(RoutingStrategy::kNone) {}
  CommandInfo(const std::string& n, int a, const std::string& f)
      : name(n), arity(a), flags(f), routing(RoutingStrategy::kNone) {}
  CommandInfo(const std::string& n, int a, const std::string& f, RoutingStrategy r)
      : name(n), arity(a), flags(f), routing(r) {}
};

// Command entry in registry
class CommandRegistry {
 public:
  CommandRegistry() = default;
  ~CommandRegistry() = default;

// Register a command
  void Register(const CommandInfo& info, CommandHandler handler) noexcept;
  
  // Unregister a command
  void Unregister(const std::string& name) noexcept;
  
  // Check if command exists
  bool Exists(const std::string& name) const noexcept;
  
  // Get command info
  const CommandInfo* GetInfo(const std::string& name) const noexcept;
  
  // Execute a command
  CommandResult Execute(const astra::protocol::Command& command, CommandContext* context) const noexcept;
  
  // Get routing strategy for a command
  RoutingStrategy GetRoutingStrategy(const std::string& command_name) const noexcept;
  
  // Get all command names
  std::vector<std::string> GetCommandNames() const noexcept;
  
  // Get number of registered commands
  size_t Size() const noexcept { return commands_.size(); }

 private:
  struct CommandEntry {
    CommandInfo info;
    CommandHandler handler;
  };

  std::unordered_map<std::string, CommandEntry> commands_;
};

// Global command registry instance
CommandRegistry& GetGlobalCommandRegistry();

}  // namespace astra::commands