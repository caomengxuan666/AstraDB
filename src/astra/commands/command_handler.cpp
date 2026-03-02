// ==============================================================================
// Command Handler Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "command_handler.hpp"
#include <unordered_map>
#include <algorithm>
#include <sstream>

namespace astra::commands {

void CommandRegistry::Register(const CommandInfo& info, CommandHandler handler) noexcept {
  CommandEntry entry;
  entry.info = info;
  entry.handler = std::move(handler);
  commands_.insert_or_assign(info.name, std::move(entry));
}

void CommandRegistry::Unregister(const std::string& name) noexcept {
  commands_.erase(name);
}

bool CommandRegistry::Exists(const std::string& name) const noexcept {
  return commands_.find(name) != commands_.end();
}

const CommandInfo* CommandRegistry::GetInfo(const std::string& name) const noexcept {
  auto it = commands_.find(name);
  if (it != commands_.end()) {
    return &it->second.info;
  }
  return nullptr;
}

CommandResult CommandRegistry::Execute(const astra::protocol::Command& command, CommandContext* context) const noexcept {
  if (command.name.empty()) {
    return CommandResult(false, "ERR empty command name");
  }

  auto it = commands_.find(command.name);
  if (it == commands_.end()) {
    return CommandResult(false, "ERR unknown command '" + command.name + "'");
  }

  const auto& entry = it->second;

  // Check arity
  if (entry.info.arity >= 0) {
    // Fixed arity
    if (static_cast<int>(command.ArgCount()) != entry.info.arity) {
      std::ostringstream oss;
      oss << "ERR wrong number of arguments for '" << command.name
          << "' command, expected " << entry.info.arity
          << ", got " << command.ArgCount();
      return CommandResult(false, oss.str());
    }
  } else {
    // Variable arity (negative means at least |arity| arguments)
    int min_args = -entry.info.arity;
    if (static_cast<int>(command.ArgCount()) < min_args) {
      std::ostringstream oss;
      oss << "ERR wrong number of arguments for '" << command.name
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
  auto it = commands_.find(command_name);
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