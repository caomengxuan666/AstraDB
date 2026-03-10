// ==============================================================================
// Command Auto-Registration - Automatic command registration system
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/synchronization/mutex.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "command_handler.hpp"

namespace astra::commands {

// Command metadata
struct CommandMetadata {
  std::string_view name;
  int arity;
  std::vector<std::string> flags;  // Changed from string_view to vector<string>
  RoutingStrategy routing;
  CommandHandler handler;
  bool is_write = false;
};

// Runtime command registry (Meyers' Singleton - function-local static)
class RuntimeCommandRegistry {
 public:
  // Function-local static singleton - thread-safe since C++11
  static RuntimeCommandRegistry& Instance() {
    static RuntimeCommandRegistry instance;
    return instance;
  }

  // Register a command (thread-safe)
  void RegisterCommand(std::string_view name, int arity, std::string_view flags,
                       RoutingStrategy routing, CommandHandler handler);

  // Apply all registered commands to a registry
  void ApplyToRegistry(CommandRegistry& registry);

  // Get command count
  size_t GetCommandCount() const {
    absl::MutexLock lock(&mutex_);
    return commands_.size();
  }

 private:
  RuntimeCommandRegistry() = default;
  ~RuntimeCommandRegistry() = default;

  // Prevent copying
  RuntimeCommandRegistry(const RuntimeCommandRegistry&) = delete;
  RuntimeCommandRegistry& operator=(const RuntimeCommandRegistry&) = delete;

  mutable absl::Mutex mutex_;
  absl::flat_hash_map<std::string, CommandMetadata> commands_;
};

// Macro for automatic command registration
// Note: ASTRADB_CONCAT is defined in base/macros.hpp
// Uses constructor-based registration that preserves function pointer
// references
#define ASTRADB_REGISTER_COMMAND(Name, Arity, Flags, Routing, Handler)       \
  namespace {                                                                \
  struct CommandRegistrar_##Name {                                           \
    CommandRegistrar_##Name() {                                              \
      ::astra::commands::RuntimeCommandRegistry::Instance().RegisterCommand( \
          #Name, Arity, Flags, Routing, Handler);                            \
    }                                                                        \
  } ASTRADB_CONCAT(g_cmd_reg_, __LINE__);                                    \
  }                                                                          \
  static_assert(true, "")

}  // namespace astra::commands
