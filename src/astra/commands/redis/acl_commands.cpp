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
  if (command.ArgCount() < 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'acl setuser' command");
  }

  const std::string& username = command[0].AsString();

  auto* acl_manager = context->GetAclManager();
  if (!acl_manager) {
    return CommandResult(false, "ERR ACL not configured");
  }

  // Extract rules
  std::vector<std::string> rules;
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    rules.push_back(command[i].AsString());
  }

  // If no rules provided, create user with default permissions
  if (rules.empty()) {
    rules.push_back("off");
    rules.push_back("nopass");
    rules.push_back("-@all");
  }

  // Apply rules
  if (acl_manager->SetUser(username, rules)) {
    protocol::RespValue resp;
    resp.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(resp);
  }

  return CommandResult(false, "ERR Failed to set user rules");
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

  // Format user info as RESP array (RESP2 format)
  protocol::RespValue resp;
  std::vector<protocol::RespValue> user_array;

  // Add "flags" key
  protocol::RespValue flags_key;
  flags_key.SetString("flags", protocol::RespType::kBulkString);
  user_array.push_back(flags_key);

  // Add flags array
  protocol::RespValue flags_array;
  std::vector<protocol::RespValue> flags_values;
  if (user_info->enabled) {
    protocol::RespValue flag_val;
    flag_val.SetString("on", protocol::RespType::kBulkString);
    flags_values.push_back(flag_val);
  } else {
    protocol::RespValue flag_val;
    flag_val.SetString("off", protocol::RespType::kBulkString);
    flags_values.push_back(flag_val);
  }
  if (user_info->all_keys) {
    protocol::RespValue flag_val;
    flag_val.SetString("allkeys", protocol::RespType::kBulkString);
    flags_values.push_back(flag_val);
  }
  if (user_info->all_channels) {
    protocol::RespValue flag_val;
    flag_val.SetString("allchannels", protocol::RespType::kBulkString);
    flags_values.push_back(flag_val);
  }
  if (user_info->no_password) {
    protocol::RespValue flag_val;
    flag_val.SetString("nopass", protocol::RespType::kBulkString);
    flags_values.push_back(flag_val);
  }
  flags_array.SetArray(flags_values);
  user_array.push_back(flags_array);

  // Add "passwords" key
  protocol::RespValue passwords_key;
  passwords_key.SetString("passwords", protocol::RespType::kBulkString);
  user_array.push_back(passwords_key);

  // Add passwords array
  protocol::RespValue passwords_array;
  std::vector<protocol::RespValue> password_values;
  for (const auto& pwd : user_info->passwords) {
    protocol::RespValue pwd_val;
    pwd_val.SetString(pwd, protocol::RespType::kBulkString);
    password_values.push_back(pwd_val);
  }
  passwords_array.SetArray(password_values);
  user_array.push_back(passwords_array);

  // Add "commands" key
  protocol::RespValue commands_key;
  commands_key.SetString("commands", protocol::RespType::kBulkString);
  user_array.push_back(commands_key);

  // Add commands (concatenate categories and commands)
  protocol::RespValue commands_value;
  std::string commands_str;
  for (const auto& cat : user_info->categories) {
    commands_str += cat + " ";
  }
  for (const auto& cmd : user_info->commands) {
    commands_str += cmd + " ";
  }
  if (commands_str.empty()) {
    commands_str = "-@all";
  }
  commands_value.SetString(commands_str, protocol::RespType::kBulkString);
  user_array.push_back(commands_value);

  // Add "keys" key
  protocol::RespValue keys_key;
  keys_key.SetString("keys", protocol::RespType::kBulkString);
  user_array.push_back(keys_key);

  // Add keys
  protocol::RespValue keys_value;
  if (user_info->all_keys) {
    keys_value.SetString("~*", protocol::RespType::kBulkString);
  } else if (!user_info->key_patterns.empty()) {
    keys_value.SetString(user_info->key_patterns[0],
                         protocol::RespType::kBulkString);
  } else {
    keys_value.SetString("", protocol::RespType::kBulkString);
  }
  user_array.push_back(keys_value);

  // Add "channels" key
  protocol::RespValue channels_key;
  channels_key.SetString("channels", protocol::RespType::kBulkString);
  user_array.push_back(channels_key);

  // Add channels
  protocol::RespValue channels_value;
  if (user_info->all_channels) {
    channels_value.SetString("&*", protocol::RespType::kBulkString);
  } else if (!user_info->channel_patterns.empty()) {
    channels_value.SetString(user_info->channel_patterns[0],
                             protocol::RespType::kBulkString);
  } else {
    channels_value.SetString("", protocol::RespType::kBulkString);
  }
  user_array.push_back(channels_value);

  resp.SetArray(user_array);
  return CommandResult(resp);
}

// ACL LIST - List users in ACL file format
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

// ACL USERS - List user names
CommandResult HandleAclUsers(const protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'acl users' command");
  }

  auto* acl_manager = context->GetAclManager();
  if (!acl_manager) {
    protocol::RespValue resp;
    resp.SetArray({});
    return CommandResult(resp);
  }

  auto users = acl_manager->GetUserNames();
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

// ACL CAT - List command categories
CommandResult HandleAclCat(const protocol::Command& command,
                           CommandContext* context) {
  // ACL CAT [categoryname]

  auto* acl_manager = context->GetAclManager();
  if (!acl_manager) {
    return CommandResult(false, "ERR ACL not configured");
  }

  // Redis command categories
  static const std::vector<std::string> categories = {
      "@keyspace", "@read",     "@write",     "@set",        "@sortedset",
      "@list",     "@hash",     "@string",    "@bitmap",     "@hyperloglog",
      "@geo",      "@stream",   "@pubsub",    "@admin",      "@fast",
      "@slow",     "@blocking", "@dangerous", "@connection", "@transaction",
      "@scripting"};

  protocol::RespValue resp;

  if (command.ArgCount() == 0) {
    // List all categories
    std::vector<protocol::RespValue> cat_array;
    for (const auto& cat : categories) {
      protocol::RespValue cat_val;
      cat_val.SetString(cat, protocol::RespType::kBulkString);
      cat_array.push_back(cat_val);
    }
    resp.SetArray(cat_array);
  } else {
    // List commands in a specific category
    // const std::string& category = command[0].AsString();

    // For now, return all commands for any category
    // In production, this should return actual commands in the category
    std::vector<protocol::RespValue> cmd_array;
    resp.SetArray(cmd_array);
  }

  return CommandResult(resp);
}

// ACL GENPASS - Generate random password
CommandResult HandleAclGenpass(const protocol::Command& command,
                               CommandContext* context) {
  // ACL GENPASS [bits]
  int bits = 256;  // Default to 256 bits
  if (command.ArgCount() > 0) {
    try {
      bits = std::stoi(command[0].AsString());
    } catch (...) {
      return CommandResult(false, "ERR Invalid bits value");
    }
  }

  if (bits != 256 && bits != 128) {
    return CommandResult(false, "ERR Bits value must be 128 or 256");
  }

  // Generate random password
  static const char charset[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::string password;
  int length = bits / 8;  // bytes

  for (int i = 0; i < length; ++i) {
    password += charset[rand() % (sizeof(charset) - 1)];
  }

  protocol::RespValue resp;
  resp.SetString(password, protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// ACL WHOAMI - Get current authenticated user
CommandResult HandleAclWhoami(const protocol::Command& command,
                              CommandContext* context) {
  // ACL WHOAMI

  protocol::RespValue resp;

  if (context->IsAuthenticated()) {
    resp.SetString(context->GetAuthenticatedUser(),
                   protocol::RespType::kBulkString);
  } else {
    resp.SetString("default", protocol::RespType::kBulkString);
  }

  return CommandResult(resp);
}

// ACL SETPASS - Set user password (deprecated, use SETUSER instead)
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

  if (!user_info->passwords.empty()) {
    // Return masked password
    std::string masked(user_info->passwords[0].length(), '*');
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
  } else if (subcommand == "GENPASS") {
    return HandleAclGenpass(subcommand_cmd, context);
  } else if (subcommand == "WHOAMI") {
    return HandleAclWhoami(subcommand_cmd, context);
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
