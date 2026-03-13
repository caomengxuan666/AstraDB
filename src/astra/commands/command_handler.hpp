// ==============================================================================
// Command Handler Interface
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/inlined_vector.h>
#include <absl/functional/any_invocable.h>
#include <absl/strings/string_view.h>
#include <absl/types/span.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "astra/network/connection.hpp"
#include "astra/protocol/resp/resp_types.hpp"
#include "astra/replication/replication_manager.hpp"
#include "command_cache_flatbuffers.hpp"
#include "database.hpp"

namespace astra::commands {
class PubSubManager;  // Forward declaration
}

// Forward declarations for optional types
namespace astra::cluster {
class GossipManager;
class ShardManager;
struct AstraNodeView;
}  // namespace astra::cluster

namespace astra::security {
class AclManager;
}

namespace astra::persistence {
class LevelDBAdapter;
class AofWriter;
}  // namespace astra::persistence

// AOF callback type for logging write commands (zero-copy with absl::Span)
using AofCallback = absl::AnyInvocable<void(
    absl::string_view command, absl::Span<const absl::string_view> args)>;

namespace astra::commands {

using astra::protocol::Command;
using astra::protocol::RespType;
using astra::protocol::RespValue;

// Forward declaration
class BlockingManager;

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
  virtual cluster::ShardManager* GetClusterShardManager() const {
    return nullptr;
  }

  // ACL operations (optional - return nullptr if not available)
  virtual security::AclManager* GetAclManager() const { return nullptr; }

  // Authenticated user
  virtual const std::string& GetAuthenticatedUser() const {
    static const std::string empty;
    return empty;
  }
  virtual void SetAuthenticatedUser(const std::string& user) { (void)user; }

  // Persistence operations (optional - return nullptr/false if not available)
  virtual bool IsPersistenceEnabled() const { return false; }
  virtual persistence::LevelDBAdapter* GetPersistence() const {
    return nullptr;
  }

  // Database manager for multi-database support (optional - return nullptr if
  // not available)
  virtual DatabaseManager* GetDatabaseManager() const { return nullptr; }

  // AOF logging callback (zero-copy)
  virtual void SetAofCallback(AofCallback callback) {
    aof_callback_ = std::move(callback);
  }
  virtual void LogToAof(absl::string_view command,
                        absl::Span<const absl::string_view> args) {
    ASTRADB_LOG_DEBUG("CommandContext::LogToAof called: command={}, args={}, has_callback={}",
                     command, args.size(), aof_callback_ != nullptr);
    if (aof_callback_) {
      aof_callback_(command, args);
    }
  }

  // ============== Transaction Support ==============
  virtual bool IsInTransaction() const { return false; }
  virtual void BeginTransaction() {}
  virtual void QueueCommand(const protocol::Command& cmd) { (void)cmd; }
  virtual absl::InlinedVector<protocol::Command, 16> GetQueuedCommands() const {
    return {};
  }
  virtual void ClearQueuedCommands() {}
  virtual void DiscardTransaction() {}
  virtual void WatchKey(const std::string& key, uint64_t version) {
    (void)key;
    (void)version;
  }
  virtual const absl::flat_hash_set<std::string>& GetWatchedKeys() const {
    static absl::flat_hash_set<std::string> empty;
    return empty;
  }
  virtual bool IsWatchedKeyModified(
      const absl::AnyInvocable<uint64_t(const std::string&) const>& get_version)
      const {
    (void)get_version;
    return false;
  }
  virtual void ClearWatchedKeys() {}

  // ============== Pub/Sub Support ==============
  virtual PubSubManager* GetPubSubManager() { return nullptr; }

  virtual replication::ReplicationManager* GetReplicationManager() {
    return nullptr;
  }
  virtual uint64_t GetConnectionId() const { return 0; }

  // Get connection pointer (optional - return nullptr if not available)
  virtual astra::network::Connection* GetConnection() const { return nullptr; }

  // ============== RESP Protocol Version Support ==============
  // Get current RESP protocol version (default to 2)
  virtual int GetProtocolVersion() const {
    auto* conn = GetConnection();
    if (conn) {
      return conn->GetProtocolVersion();
    }
    return 2;  // Default to RESP2
  }

  // Set RESP protocol version
  virtual void SetProtocolVersion(int version) {
    auto* conn = GetConnection();
    if (conn) {
      conn->SetProtocolVersion(version);
    }
  }

  // Get server pointer (optional - return nullptr if not available)
  virtual void* GetServer() const { return nullptr; }

  // Get blocking manager (optional - return nullptr if not available)
  virtual class BlockingManager* GetBlockingManager() { return nullptr; }

 protected:
  AofCallback aof_callback_;
};

// Command result
struct CommandResult {
  RespValue response;
  bool success = true;
  std::string error;
  bool is_blocking = false;  // Indicates if command is in blocking state

  CommandResult() = default;
  explicit CommandResult(RespValue resp) : response(std::move(resp)) {}
  CommandResult(bool ok, std::string err)
      : success(ok), error(std::move(err)) {}

  // Create a blocking result (command will respond later)
  static CommandResult Blocking() {
    CommandResult result;
    result.is_blocking = true;
    return result;
  }

  // Check if result is in blocking state
  bool IsBlocking() const { return is_blocking; }
};

// Command handler function signature
// Using std::function instead of absl::AnyInvocable to support copying
// (needed for NO SHARING architecture where each Worker has its own CommandRegistry)
using CommandHandler = std::function<CommandResult(
    const astra::protocol::Command& command, CommandContext* context)>;

// Command routing strategy
enum class RoutingStrategy {
  kNone,        // No routing (use default shard)
  kByFirstKey,  // Route based on first argument (key)
  kByArgument,  // Route based on specific argument index
  kAllShards,   // Broadcast to all shards (not implemented yet)
};

// Command metadata
struct CommandInfo {
  std::string name;
  int arity;  // Number of arguments. Positive for fixed, negative for variable
              // (e.g., -2 means at least 1)
  std::vector<std::string>
      flags;  // Command flags (readonly, fast, etc.) - array of strings
  RoutingStrategy routing;  // How to route this command
  bool is_write = false;    // Whether this command modifies data

  CommandInfo() : routing(RoutingStrategy::kNone) {}
  CommandInfo(const std::string& n, int a, const std::vector<std::string>& f,
              bool w = false)
      : name(n),
        arity(a),
        flags(f),
        routing(RoutingStrategy::kNone),
        is_write(w) {
    // Auto-detect is_write from flags if not explicitly set
    if (!w && std::find(f.begin(), f.end(), "write") != f.end()) {
      is_write = true;
    }
  }
  CommandInfo(const std::string& n, int a, const std::vector<std::string>& f,
              RoutingStrategy r, bool w = false)
      : name(n), arity(a), flags(f), routing(r), is_write(w) {
    // Auto-detect is_write from flags if not explicitly set
    if (!w && std::find(f.begin(), f.end(), "write") != f.end()) {
      is_write = true;
    }
  }
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
  CommandResult Execute(const astra::protocol::Command& command,
                        CommandContext* context) const noexcept;

  // Get routing strategy for a command
  RoutingStrategy GetRoutingStrategy(
      const std::string& command_name) const noexcept;

  // Get all command names
  std::vector<std::string> GetCommandNames() const noexcept;

  // Get number of registered commands
  size_t Size() const noexcept { return commands_.size(); }

  // ========== Command Parameter Caching ==========

  // Enable/disable command parameter caching
  void EnableCaching(bool enable) noexcept { caching_enabled_ = enable; }
  bool IsCachingEnabled() const noexcept { return caching_enabled_; }

  // Get cache statistics
  struct CacheStats {
    uint64_t hit_count = 0;
    uint64_t miss_count = 0;
    uint32_t entry_count = 0;
    uint32_t eviction_count = 0;
  };

  CacheStats GetCacheStats() const noexcept {
    CacheStats stats;
    stats.hit_count = cache_hit_count_;
    stats.miss_count = cache_miss_count_;
    stats.entry_count = static_cast<uint32_t>(command_cache_.size());
    stats.eviction_count = cache_eviction_count_;
    return stats;
  }

  // Clear cache
  void ClearCache() noexcept {
    command_cache_.clear();
    cache_hit_count_ = 0;
    cache_miss_count_ = 0;
    cache_eviction_count_ = 0;
  }

  // Export cache to FlatBuffers
  std::vector<uint8_t> ExportCacheSnapshot() const noexcept {
    std::vector<CachedCommandEntry> entries;
    entries.reserve(command_cache_.size());

    for (const auto& [key, entry] : command_cache_) {
      entries.push_back(entry);
    }

    return CommandCacheFlatbuffersSerializer::SerializeCacheSnapshot(
        entries, cache_hit_count_, cache_miss_count_, cache_eviction_count_);
  }

  // Import cache from FlatBuffers
  bool ImportCacheSnapshot(const uint8_t* data, size_t size) noexcept {
    std::vector<CachedCommandEntry> entries;
    uint64_t hit_count = 0;
    uint64_t miss_count = 0;
    uint32_t eviction_count = 0;

    if (!CommandCacheFlatbuffersSerializer::DeserializeCacheSnapshot(
            data, size, entries, hit_count, miss_count, eviction_count)) {
      return false;
    }

    // Import entries
    for (const auto& entry : entries) {
      command_cache_[entry.hash] = entry;
    }

    cache_hit_count_ = hit_count;
    cache_miss_count_ = miss_count;
    cache_eviction_count_ = eviction_count;

    return true;
  }

 private:
  struct CommandEntry {
    CommandInfo info;
    CommandHandler handler;
  };

  absl::flat_hash_map<std::string, CommandEntry> commands_;

  // Command parameter caching
  mutable bool caching_enabled_ = false;
  mutable absl::flat_hash_map<std::string, CachedCommandEntry> command_cache_;
  mutable uint64_t cache_hit_count_ = 0;
  mutable uint64_t cache_miss_count_ = 0;
  mutable uint32_t cache_eviction_count_ = 0;

  // Maximum cache size (LRU eviction when exceeded)
  static constexpr size_t kMaxCacheSize = 10000;

  // Helper functions for caching
  std::string GenerateCacheKey(
      const astra::protocol::Command& command) const noexcept;
  void EvictLRUCacheEntry() const noexcept;
};

// Global command registry instance
CommandRegistry& GetGlobalCommandRegistry();

// Set global command registry instance (called by Server)
void SetGlobalCommandRegistry(CommandRegistry* registry);

}  // namespace astra::commands
