// ==============================================================================
// Command Auto-Registration Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "command_auto_register.hpp"

#include <absl/synchronization/mutex.h>

namespace astra::commands {

// Helper function to parse comma-separated flags string into vector<string>
static std::vector<std::string> ParseFlagsString(std::string_view flags_str) {
  std::vector<std::string> result;
  if (flags_str.empty()) {
    return result;
  }

  std::string str(flags_str);
  size_t start = 0, end = 0;
  while ((end = str.find(',', start)) != std::string::npos) {
    std::string flag = str.substr(start, end - start);
    // Trim whitespace
    flag.erase(0, flag.find_first_not_of(" \t"));
    flag.erase(flag.find_last_not_of(" \t") + 1);
    if (!flag.empty()) {
      result.push_back(flag);
    }
    start = end + 1;
  }
  // Handle last flag
  std::string flag = str.substr(start);
  flag.erase(0, flag.find_first_not_of(" \t"));
  flag.erase(flag.find_last_not_of(" \t") + 1);
  if (!flag.empty()) {
    result.push_back(flag);
  }

  return result;
}

void RuntimeCommandRegistry::RegisterCommand(std::string_view name, int arity,
                                             std::string_view flags,
                                             RoutingStrategy routing,
                                             CommandHandler handler) {
  absl::MutexLock lock(&mutex_);

  // Parse flags from comma-separated string to vector<string>
  auto flags_array = ParseFlagsString(flags);
  bool is_write = std::find(flags_array.begin(), flags_array.end(), "write") !=
                  flags_array.end();

  // Store the parsed flags array directly (not the original string)
  commands_[std::string(name)] = {
      name, arity, flags_array, routing, std::move(handler), is_write};
}

// Apply all registered commands to a registry
void RuntimeCommandRegistry::ApplyToRegistry(CommandRegistry& registry) {
  absl::MutexLock lock(&mutex_);
  for (auto& [name, cmd] : commands_) {
    // Use the pre-parsed flags array directly
    // Copy handler instead of moving to allow multiple calls
    registry.Register(CommandInfo(std::string(cmd.name), cmd.arity, cmd.flags,
                                  cmd.routing, cmd.is_write),
                      cmd.handler);  // Copy instead of move
  }
}

}  // namespace astra::commands
