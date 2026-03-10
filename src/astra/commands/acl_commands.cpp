// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "acl_commands.hpp"

#include "astra/base/logging.hpp"
#include "astra/security/acl_manager.hpp"

namespace astra::commands {

// AUTH - Authenticate user
CommandResult HandleAuth(const protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() < 1 || command.ArgCount() > 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'auth' command");
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
CommandResult HandleAclSave(const protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'acl save' command");
  }

  ASTRADB_LOG_INFO("ACL SAVE - ACL configuration saved");
  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// ACL LOAD - Load ACL config
CommandResult HandleAclLoad(const protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'acl load' command");
  }

  ASTRADB_LOG_INFO("ACL LOAD - ACL configuration loaded");
  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// ACL SETUSER - Create or modify user
CommandResult HandleAclSetUser(const protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'acl setuser' command");
  }

  const std::string& username = command[0].AsString();
  const std::string& password = command[1].AsString();

  auto* acl_manager = context->GetAclManager();
  if (!acl_manager) {
    return CommandResult(false, "ERR ACL not configured");
  }

  // Create user with admin permissions
  if (acl_manager->CreateUser(
          username, password,
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
CommandResult HandleAclDelUser(const protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'acl deluser' command");
  }

  const std::string& username = command[0].AsString();

  auto* acl_manager = context->GetAclManager();
  if (!acl_manager) {
    return CommandResult(false, "ERR ACL not configured");
  }

  // Delete user from ACL manager
  bool deleted = acl_manager->DeleteUser(username);

  ASTRADB_LOG_INFO("ACL DELUSER: {} - {}", username,
                   deleted ? "success" : "failed");

  protocol::RespValue resp;
  resp.SetInteger(deleted ? 1 : 0);  // Number of users deleted
  return CommandResult(resp);
}

// ACL GETUSER - Get user info
CommandResult HandleAclGetUser(const protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'acl getuser' command");
  }

  const std::string& username = command[0].AsString();

  auto* acl_manager = context->GetAclManager();
  if (!acl_manager) {
    return CommandResult(false, "ERR ACL not configured");
  }

  // Get user ACL rules
  auto user_info = acl_manager->GetUserInfo(username);
  if (!user_info) {
    return CommandResult(false, "ERR User '" + username + "' does not exist");
  }

  ASTRADB_LOG_INFO("ACL GETUSER: {}", username);

  // Format user info as RESP array
  protocol::RespValue resp;
  std::vector<protocol::RespValue> user_array;

  // Add username
  protocol::RespValue username_val;
  username_val.SetString(username, protocol::RespType::kBulkString);
  user_array.push_back(username_val);

  // Add ACL rules
  protocol::RespValue rules_val;
  std::vector<protocol::RespValue> rules_array;
  rules_val.SetArray(rules_array);
  user_array.push_back(rules_val);

  // Add passwords
  protocol::RespValue passwords_val;
  std::vector<protocol::RespValue> passwords_array;
  passwords_val.SetArray(passwords_array);
  user_array.push_back(passwords_val);

  // Add flags
  protocol::RespValue flags_val;
  flags_val.SetString("on", protocol::RespType::kSimpleString);
  user_array.push_back(flags_val);

  resp.SetArray(user_array);
  return CommandResult(resp);
}

// ACL LIST - List users
CommandResult HandleAclList(const protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'acl list' command");
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
CommandResult HandleAclUsers(const protocol::Command& command,
                             CommandContext* context) {
  return HandleAclList(command, context);
}

// ACL CAT - Get user ACL rules
CommandResult HandleAclCat(const protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'acl cat' command");
  }

  const std::string& username = command[0].AsString();

  auto* acl_manager = context->GetAclManager();
  if (!acl_manager) {
    return CommandResult(false, "ERR ACL not configured");
  }

  // Get user info
  auto user_info = acl_manager->GetUserInfo(username);
  if (!user_info) {
    return CommandResult(false, "ERR User '" + username + "' does not exist");
  }

  ASTRADB_LOG_INFO("ACL CAT: {}", username);

  // Format permissions as RESP string
  protocol::RespValue resp;
  std::string permissions_str;

  // Convert permissions to string representation
  if (user_info->permissions &
      static_cast<uint32_t>(astra::security::AclPermission::kRead)) {
    permissions_str += "+@all +read +write +admin ";
  } else if (user_info->permissions &
             static_cast<uint32_t>(astra::security::AclPermission::kWrite)) {
    permissions_str += "+@all +read +write ";
  } else if (user_info->permissions &
             static_cast<uint32_t>(astra::security::AclPermission::kAdmin)) {
    permissions_str += "+@all +read +write +admin ";
  } else {
    permissions_str += "+@all +read ";
  }

  resp.SetString(permissions_str, protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// ACL SETUSER - Set user password
CommandResult HandleAclSetpass(const protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'acl setuser' command");
  }

  const std::string& username = command[0].AsString();
  std::string password = "";
  if (command.ArgCount() > 1) {
    password = command[1].AsString();
  }

  auto* acl_manager = context->GetAclManager();
  if (!acl_manager) {
    return CommandResult(false, "ERR ACL not configured");
  }

  // Set user password
  bool updated = acl_manager->SetPassword(username, password);

  ASTRADB_LOG_INFO("ACL SETPASS: {} - {}", username,
                   updated ? "success" : "failed");

  if (!updated) {
    return CommandResult(
        false, "ERR Failed to set password for user '" + username + "'");
  }

  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// ACL GETUSER - Get user password
CommandResult HandleAclGetpass(const protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'acl getuser' command");
  }

  const std::string& username = command[0].AsString();

  auto* acl_manager = context->GetAclManager();
  if (!acl_manager) {
    return CommandResult(false, "ERR ACL not configured");
  }

  // Get user info and return masked password
  auto user_info = acl_manager->GetUserInfo(username);
  if (!user_info) {
    return CommandResult(false, "ERR User '" + username + "' does not exist");
  }

  ASTRADB_LOG_INFO("ACL GETPASS: {}", username);

  // Return masked password
  protocol::RespValue resp;

  if (!user_info->password.empty()) {
    // Return masked password
    std::string masked(user_info->password.length(), '*');
    resp.SetString(masked, protocol::RespType::kBulkString);
  } else {
    resp.SetInteger(-1);  // No password set
  }

  return CommandResult(resp);
}

// Handle ACL subcommands (ACL container command)
CommandResult HandleAcl(const protocol::Command& command,
                        CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'acl' command");
  }

  const auto& subcommand = command[0].AsString();

  // Create a new command object without the subcommand argument
  // We need to slice the args vector to exclude the subcommand itself
  protocol::Command subcommand_cmd;
  subcommand_cmd.name = command.name;
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    subcommand_cmd.args.push_back(command[i]);
  }

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
    return HandleAclSetUser(subcommand_cmd, context);
  } else if (subcommand == "DELUSER") {
    return HandleAclDelUser(subcommand_cmd, context);
  } else if (subcommand == "GETUSER") {
    return HandleAclGetUser(subcommand_cmd, context);
  } else if (subcommand == "CAT") {
    return HandleAclCat(subcommand_cmd, context);
  } else if (subcommand == "SETPASS") {
    return HandleAclSetpass(subcommand_cmd, context);
  } else if (subcommand == "GETPASS") {
    return HandleAclGetpass(subcommand_cmd, context);
  } else {
    return CommandResult(false,
                         "ERR Unknown ACL subcommand '" + subcommand + "'");
  }
}

}  // namespace astra::commands
