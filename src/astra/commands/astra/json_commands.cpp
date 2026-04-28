// ==============================================================================
// JSON Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "json_commands.hpp"

#include <algorithm>
#include <sstream>

#include "astra/protocol/resp/resp_builder.hpp"
#include "../command_auto_register.hpp"
#include "../database.hpp"

namespace astra::commands {

using astra::protocol::RespType;
using astra::protocol::RespValue;

namespace {

RespValue MakeOk() {
  RespValue r;
  r.SetString(std::string("OK"), RespType::kSimpleString);
  return r;
}

}  // namespace

CommandResult HandleJsonSet(const protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'JSON.SET'");
  }

  std::string key = command[0].AsString();
  std::string path = "$";
  std::string value;
  if (command.ArgCount() == 2) {
    value = command[1].AsString();
  } else {
    path = command[1].AsString();
    value = command[2].AsString();
  }

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  if (path != "$") {
    return CommandResult(false, "ERR only root path $ is supported for JSON.SET");
  }

  if (!db->JsonSet(key, value)) {
    return CommandResult(false, "ERR invalid JSON value");
  }
  return CommandResult(MakeOk());
}

CommandResult HandleJsonGet(const protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'JSON.GET'");
  }

  std::string key = command[0].AsString();
  std::string path = "$";
  if (command.ArgCount() > 1) path = command[1].AsString();

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  auto result = db->JsonGet(key, path);
  if (!result.has_value()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }
  return CommandResult(RespValue(std::string(*result)));
}

CommandResult HandleJsonDel(const protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'JSON.DEL'");
  }

  std::string key = command[0].AsString();
  std::string path = "$";
  if (command.ArgCount() > 1) path = command[1].AsString();

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  bool ok = db->JsonDelete(key, path);
  return CommandResult(RespValue(static_cast<int64_t>(ok ? 1 : 0)));
}

CommandResult HandleJsonType(const protocol::Command& command,
                              CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'JSON.TYPE'");
  }

  std::string key = command[0].AsString();
  std::string path = "$";
  if (command.ArgCount() > 1) path = command[1].AsString();

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  auto result = db->JsonType(key, path);
  if (!result.has_value()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }
  return CommandResult(RespValue(std::string(*result)));
}

CommandResult HandleJsonArrAppend(const protocol::Command& command,
                                   CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'JSON.ARRAPPEND'");
  }

  std::string key = command[0].AsString();
  std::string path = command[1].AsString();

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  size_t count = 0;
  for (size_t i = 2; i < command.ArgCount(); i++) {
    if (db->JsonArrayAppend(key, path, command[i].AsString())) count++;
  }
  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

CommandResult HandleJsonArrLen(const protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'JSON.ARRLEN'");
  }

  std::string key = command[0].AsString();
  std::string path = "$";
  if (command.ArgCount() > 1) path = command[1].AsString();

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  auto len = db->JsonArrayLen(key, path);
  if (!len.has_value()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }
  return CommandResult(RespValue(static_cast<int64_t>(*len)));
}

CommandResult HandleJsonObjLen(const protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'JSON.OBJLEN'");
  }

  std::string key = command[0].AsString();
  std::string path = "$";
  if (command.ArgCount() > 1) path = command[1].AsString();

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  auto len = db->JsonObjLen(key, path);
  if (!len.has_value()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }
  return CommandResult(RespValue(static_cast<int64_t>(*len)));
}

CommandResult HandleJsonNumIncrBy(const protocol::Command& command,
                                   CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'JSON.NUMINCRBY'");
  }

  std::string key = command[0].AsString();
  std::string path = command[1].AsString();
  int64_t inc = command[2].AsInteger();

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  auto result = db->JsonNumIncrBy(key, path, inc);
  if (!result.has_value()) {
    return CommandResult(false, "ERR path not found or not a number");
  }
  return CommandResult(RespValue(*result));
}

CommandResult HandleJsonStrAppend(const protocol::Command& command,
                                   CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'JSON.STRAPPEND'");
  }

  std::string key = command[0].AsString();
  std::string path = command[1].AsString();
  std::string str = command[2].AsString();

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  if (!db->JsonStrAppend(key, path, str)) {
    return CommandResult(false, "ERR path not found or not a string");
  }
  return CommandResult(MakeOk());
}

CommandResult HandleJsonArrIndex(const protocol::Command& command,
                                  CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'JSON.ARRINDEX'");
  }

  std::string key = command[0].AsString();
  std::string path = command[1].AsString();
  std::string val = command[2].AsString();

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  auto idx = db->JsonArrIndex(key, path, val);
  if (!idx.has_value()) {
    return CommandResult(false, "ERR path not found or not an array");
  }
  return CommandResult(RespValue(*idx));
}

namespace {
struct JsonCommandRegistrar {
  JsonCommandRegistrar() {
    auto& reg = ::astra::commands::RuntimeCommandRegistry::Instance();
    reg.RegisterCommand("JSON.SET", -3, "write", RoutingStrategy::kByFirstKey, HandleJsonSet);
    reg.RegisterCommand("JSON.GET", -2, "readonly", RoutingStrategy::kByFirstKey, HandleJsonGet);
    reg.RegisterCommand("JSON.DEL", -2, "write", RoutingStrategy::kByFirstKey, HandleJsonDel);
    reg.RegisterCommand("JSON.TYPE", -2, "readonly", RoutingStrategy::kByFirstKey, HandleJsonType);
    reg.RegisterCommand("JSON.ARRAPPEND", -4, "write", RoutingStrategy::kByFirstKey, HandleJsonArrAppend);
    reg.RegisterCommand("JSON.ARRLEN", -2, "readonly", RoutingStrategy::kByFirstKey, HandleJsonArrLen);
    reg.RegisterCommand("JSON.OBJLEN", -2, "readonly", RoutingStrategy::kByFirstKey, HandleJsonObjLen);
    reg.RegisterCommand("JSON.NUMINCRBY", 4, "write", RoutingStrategy::kByFirstKey, HandleJsonNumIncrBy);
    reg.RegisterCommand("JSON.STRAPPEND", 4, "write", RoutingStrategy::kByFirstKey, HandleJsonStrAppend);
    reg.RegisterCommand("JSON.ARRINDEX", 4, "readonly", RoutingStrategy::kByFirstKey, HandleJsonArrIndex);
  }
} g_json_registrar;
}  // namespace

}  // namespace astra::commands
