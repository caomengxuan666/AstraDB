// ==============================================================================
// Command Auto-Registration Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "command_auto_register.hpp"

namespace astra::commands {

// Register a command (non-inline to ensure cross-TU references)
// This implementation must be in a separate compilation unit to create
// bidirectional references that prevent LTO from optimizing away command files
void RuntimeCommandRegistry::RegisterCommand(
    std::string_view name,
    int arity,
    std::string_view flags,
    RoutingStrategy routing,
    CommandHandler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Auto-detect is_write from flags
  bool is_write = (flags == "write");
  // Store the handler (function pointer) - this creates a reference back
  // to the command function, preventing linker from optimizing it away
  commands_[std::string(name)] = {name, arity, flags, routing, handler, is_write};
}

// Apply all registered commands to a registry
void RuntimeCommandRegistry::ApplyToRegistry(CommandRegistry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& [name, cmd] : commands_) {
    registry.Register(
        CommandInfo(std::string(cmd.name), cmd.arity,
                   std::string(cmd.flags), cmd.routing),
        cmd.handler);
  }
}

}  // namespace astra::commands