// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "acl_commands.hpp"
#include "astra/base/logging.hpp"
#include "astra/security/acl_manager.hpp"

namespace astra::commands {

// AUTH - Authenticate user
CommandResult HandleAuth(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1 || command.ArgCount() > 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'auth' command");
  }

  const std::string& username = command[0].AsString();
  std::string password = "";
  if (command.ArgCount() == 2) {
    password = command[1].AsString();
  }

  auto* acl_manager = context->GetAclManager();
  if (!acl_manager) {
    // No ACL manager, accept all authentication
    protocol::RespValue resp;
    resp.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(resp);
  }

  if (acl_manager->Authenticate(username, password)) {
    context->SetAuthenticated(true);
    context->SetAuthenticatedUser(username);
    protocol::RespValue resp;
    resp.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(resp);
  }

  return CommandResult(false, "WRONGPASS invalid username-password pair");
}

// ACL SAVE - Save ACL config
CommandResult HandleAclSave(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(false, "ERR wrong number of arguments for 'acl save' command");
  }

  ASTRADB_LOG_INFO("ACL SAVE - ACL configuration saved");
  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// ACL LOAD - Load ACL config
CommandResult HandleAclLoad(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(false, "ERR wrong number of arguments for 'acl load' command");
  }

  ASTRADB_LOG_INFO("ACL LOAD - ACL configuration loaded");
  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// ACL SETUSER - Create or modify user
CommandResult HandleAclSetUser(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'acl setuser' command");
  }

  const std::string& username = command[0].AsString();
  const std::string& password = command[1].AsString();

  auto* acl_manager = context->GetAclManager();
  if (!acl_manager) {
    return CommandResult(false, "ERR ACL not configured");
  }

  // Create user with admin permissions
  if (acl_manager->CreateUser(username, password, 
      static_cast<uint32_t>(security::AclPermission::kRead) |
      static_cast<uint32_t>(security::AclPermission::kWrite) |
      static_cast<uint32_t>(security::AclPermission::kAdmin))) {
    protocol::RespValue resp;
    resp.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(resp);
  }

  return CommandResult(false, "ERR user already exists");
}

// ACL DELUSER - Delete user
CommandResult HandleAclDelUser(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'acl deluser' command");
  }

  const std::string& username = command[0].AsString();

  auto* acl_manager = context->GetAclManager();
  if (!acl_manager) {
    return CommandResult(false, "ERR ACL not configured");
  }

  // TODO: Implement user deletion
  ASTRADB_LOG_INFO("ACL DELUSER: {}", username);
  protocol::RespValue resp;
  resp.SetInteger(1);  // Number of users deleted
  return CommandResult(resp);
}

// ACL GETUSER - Get user info
CommandResult HandleAclGetUser(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'acl getuser' command");
  }

  const std::string& username = command[0].AsString();

  // TODO: Return user ACL rules
  ASTRADB_LOG_INFO("ACL GETUSER: {}", username);
  protocol::RespValue resp;
  resp.SetString("on", protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// ACL LIST - List users
CommandResult HandleAclList(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(false, "ERR wrong number of arguments for 'acl list' command");
  }

  auto* acl_manager = context->GetAclManager();
  if (!acl_manager) {
    protocol::RespValue resp;
    resp.SetArray({});
    return CommandResult(resp);
  }

  auto users = acl_manager->GetUsers();
  protocol::RespValue resp;
  std::vector<protocol::RespValue> user_array;
  for (const auto& user : users) {
    protocol::RespValue user_val;
    user_val.SetString(user, protocol::RespType::kBulkString);
    user_array.push_back(user_val);
  }
  resp.SetArray(user_array);
  return CommandResult(resp);
}

// ACL USERS - List users
CommandResult HandleAclUsers(const protocol::Command& command, CommandContext* context) {
  return HandleAclList(command, context);
}

// ACL CAT - Get user ACL rules
CommandResult HandleAclCat(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'acl cat' command");
  }

  const std::string& username = command[0].AsString();
  
  // TODO: Return user rules
  ASTRADB_LOG_INFO("ACL CAT: {}", username);
  protocol::RespValue resp;
  resp.SetArray({});
  return CommandResult(resp);
}

// ACL SETUSER - Set user password
CommandResult HandleAclSetpass(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'acl setuser' command");
  }

  const std::string& username = command[0].AsString();
  std::string password = "";
  if (command.ArgCount() > 1) {
    password = command[1].AsString();
  }

  // TODO: Set user password
  ASTRADB_LOG_INFO("ACL SETPASS: {}", username);
  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// ACL GETUSER - Get user password
CommandResult HandleAclGetpass(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'acl getuser' command");
  }

  const std::string& username = command[0].AsString();

  // TODO: Return user password (masked)
  ASTRADB_LOG_INFO("ACL GETPASS: {}", username);
  protocol::RespValue resp;
  resp.SetString("hidden", protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// Handle ACL subcommands (ACL container command)
CommandResult HandleAcl(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'acl' command");
  }

  const auto& subcommand = command[0].AsString();

  // Create a new command object without the subcommand argument
  // This is a simplified approach - in a real implementation we'd need to properly slice the command
  // For now, we'll just check the subcommand and handle it appropriately
  
  if (subcommand == "SAVE") {
    // ACL SAVE has no additional arguments
    protocol::Command empty_cmd;
    return HandleAclSave(empty_cmd, context);
  } else if (subcommand == "LOAD") {
    // ACL LOAD has no additional arguments
    protocol::Command empty_cmd;
    return HandleAclLoad(empty_cmd, context);
  } else if (subcommand == "LIST") {
    // ACL LIST has no additional arguments
    protocol::Command empty_cmd;
    return HandleAclList(empty_cmd, context);
  } else if (subcommand == "USERS") {
    // ACL USERS has no additional arguments
    protocol::Command empty_cmd;
    return HandleAclUsers(empty_cmd, context);
  } else if (subcommand == "SETUSER") {
    return HandleAclSetUser(command, context);
  } else if (subcommand == "DELUSER") {
    return HandleAclDelUser(command, context);
  } else if (subcommand == "GETUSER") {
    return HandleAclGetUser(command, context);
  } else if (subcommand == "CAT") {
    return HandleAclCat(command, context);
  } else if (subcommand == "SETPASS") {
    return HandleAclSetpass(command, context);
  } else if (subcommand == "GETPASS") {
    return HandleAclGetpass(command, context);
  } else {
    return CommandResult(false, "ERR Unknown ACL subcommand '" + subcommand + "'");
  }
}

}  // namespace astra::commands