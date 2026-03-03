// ==============================================================================
// Command Handler Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "command_handler.hpp"
#include <absl/strings/ascii.h>
#include <unordered_map>
#include <algorithm>
#include <sstream>

namespace astra::commands {

void CommandRegistry::Register(const CommandInfo& info, CommandHandler handler) noexcept {
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

const CommandInfo* CommandRegistry::GetInfo(const std::string& name) const noexcept {
  std::string upper_name = absl::AsciiStrToUpper(name);
  auto it = commands_.find(upper_name);
  if (it != commands_.end()) {
    return &it->second.info;
  }
  return nullptr;
}

CommandResult CommandRegistry::Execute(const astra::protocol::Command& command, CommandContext* context) const noexcept {
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
  if (entry.info.arity >= 0) {
    // Fixed arity
    if (static_cast<int>(command.ArgCount()) != entry.info.arity) {
      std::ostringstream oss;
      oss << "ERR wrong number of arguments for '" << upper_name
          << "' command, expected " << entry.info.arity
          << ", got " << command.ArgCount();
      return CommandResult(false, oss.str());
    }
  } else {
    // Variable arity (negative means at least |arity| arguments)
    int min_args = -entry.info.arity;
    if (static_cast<int>(command.ArgCount()) < min_args) {
      std::ostringstream oss;
      oss << "ERR wrong number of arguments for '" << upper_name
          << "' command, expected at least " << min_args
          << ", got " << command.ArgCount();
      return CommandResult(false, oss.str());
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
  std::vector<std::string> names;
  names.reserve(commands_.size());
  for (const auto& pair : commands_) {
    names.push_back(pair.first);
  }
  std::sort(names.begin(), names.end());
  return names;
}

RoutingStrategy CommandRegistry::GetRoutingStrategy(const std::string& command_name) const noexcept {
  std::string upper_name = absl::AsciiStrToUpper(command_name);
  auto it = commands_.find(upper_name);
  if (it != commands_.end()) {
    return it->second.info.routing;
  }
  return RoutingStrategy::kNone;
}

CommandRegistry& GetGlobalCommandRegistry() {
  static CommandRegistry registry;
  return registry;
}

}  // namespace astra::commands