// ==============================================================================
// Optimized Command Registry with Static Command Table
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/container/flat_hash_map.h>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "astra/base/simd_utils.hpp"
#include "command_handler.hpp"

namespace astra::commands {

// ==============================================================================
// Command Hash Function with FNV-1a
// ==============================================================================

struct CommandHash {
  using is_transparent = void;

  // Compile-time FNV-1a hash for static strings
  static constexpr uint64_t kFNVOffsetBasis = 14695981039346656037ULL;
  static constexpr uint64_t kFNVPrime = 1099511628211ULL;

  // Runtime hash
  size_t operator()(std::string_view str) const {
    uint64_t hash = kFNVOffsetBasis;
    for (char c : str) {
      hash ^= static_cast<uint64_t>(c);
      hash *= kFNVPrime;
    }
    return static_cast<size_t>(hash);
  }

  // Hash for std::string
  size_t operator()(const std::string& str) const {
    return this->operator()(std::string_view(str));
  }
};

// ==============================================================================
// Command Equality with SIMD-accelerated comparison
// ==============================================================================

struct CommandEqual {
  using is_transparent = void;

  bool operator()(std::string_view lhs, std::string_view rhs) const {
    if (lhs.size() != rhs.size()) {
      return false;
    }
    // Use SIMD for larger strings
    if (lhs.size() >= 16) {
      return astra::base::simd::CaseInsensitiveEquals(lhs.data(), rhs.data(), lhs.size());
    }
    // Direct comparison for small strings
    return lhs == rhs;
  }

  bool operator()(std::string_view lhs, const std::string& rhs) const {
    return this->operator()(lhs, std::string_view(rhs));
  }

  bool operator()(const std::string& lhs, std::string_view rhs) const {
    return this->operator()(std::string_view(lhs), rhs);
  }

  bool operator()(const std::string& lhs, const std::string& rhs) const {
    return this->operator()(std::string_view(lhs), std::string_view(rhs));
  }
};

// ==============================================================================
// Optimized Command Registry
// ==============================================================================

class OptimizedCommandRegistry final {
 public:
  using MapType = absl::flat_hash_map<std::string, CommandEntry, CommandHash, CommandEqual>;

  OptimizedCommandRegistry() {
    // Reserve space for common commands
    registry_.reserve(64);
  }

  ~OptimizedCommandRegistry() = default;

  ASTRABI_DISABLE_COPY_MOVE(OptimizedCommandRegistry)

  // Register a command
  void Register(const CommandInfo& info, CommandHandler handler) noexcept {
    CommandEntry entry;
    entry.info = info;
    entry.handler = std::move(handler);
    registry_.insert_or_assign(info.name, std::move(entry));
  }

  // Unregister a command
  void Unregister(std::string_view name) noexcept {
    registry_.erase(name);
  }

  // Check if command exists
  bool Exists(std::string_view name) const noexcept {
    return registry_.find(name) != registry_.end();
  }

  // Get command info
  const CommandInfo* GetInfo(std::string_view name) const noexcept {
    auto it = registry_.find(name);
    if (it != registry_.end()) {
      return &it->second.info;
    }
    return nullptr;
  }

  // Execute a command
  CommandResult Execute(const astra::protocol::Command& command, CommandContext* context) const noexcept {
    if (command.name.empty()) {
      return CommandResult(false, "ERR empty command name");
    }

    auto it = registry_.find(std::string_view(command.name));
    if (it == registry_.end()) {
      return CommandResult(false, "ERR unknown command '" + command.name + "'");
    }

    const auto& entry = it->second;

    // Check arity
    if (entry.info.arity >= 0) {
      // Fixed arity
      if (static_cast<int>(command.ArgCount()) != entry.info.arity) {
        return CommandResult(false, "ERR wrong number of arguments for '" + command.name + "' command");
      }
    } else {
      // Variable arity (negative means at least |arity| arguments)
      int min_args = -entry.info.arity;
      if (static_cast<int>(command.ArgCount()) < min_args) {
        return CommandResult(false, "ERR wrong number of arguments for '" + command.name + "' command");
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

  // Get all command names
  std::vector<std::string> GetCommandNames() const noexcept {
    std::vector<std::string> names;
    names.reserve(registry_.size());
    for (const auto& pair : registry_) {
      names.push_back(pair.first);
    }
    std::sort(names.begin(), names.end());
    return names;
  }

  // Get routing strategy
  RoutingStrategy GetRoutingStrategy(std::string_view command_name) const noexcept {
    auto it = registry_.find(command_name);
    if (it != registry_.end()) {
      return it->second.info.routing;
    }
    return RoutingStrategy::kNone;
  }

  // Get size
  size_t Size() const noexcept {
    return registry_.size();
  }

 private:
  MapType registry_;
};

// ==============================================================================
// Singleton optimized command registry
// ==============================================================================

inline OptimizedCommandRegistry& GetOptimizedCommandRegistry() {
  static OptimizedCommandRegistry registry;
  return registry;
}

}  // namespace astra::commands