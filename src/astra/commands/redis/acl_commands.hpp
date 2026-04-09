// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "astra/commands/command_handler.hpp"
#include "astra/protocol/resp/resp_types.hpp"
#include "astra/security/acl_manager.hpp"

namespace astra::commands {

// AUTH - Authenticate user
CommandResult HandleAuth(const protocol::Command& command,
                         CommandContext* context);

// ACL container command - dispatches to subcommands
CommandResult HandleAcl(const protocol::Command& command,
                        CommandContext* context);

// ACL SAVE - Save ACL config
CommandResult HandleAclSave(const protocol::Command& command,
                            CommandContext* context);

// ACL LOAD - Load ACL config
CommandResult HandleAclLoad(const protocol::Command& command,
                            CommandContext* context);

// ACL SETUSER - Create or modify user
CommandResult HandleAclSetUser(const protocol::Command& command,
                               CommandContext* context);

// ACL DELUSER - Delete user
CommandResult HandleAclDelUser(const protocol::Command& command,
                               CommandContext* context);

// ACL GETUSER - Get user info
CommandResult HandleAclGetUser(const protocol::Command& command,
                               CommandContext* context);

// ACL LIST - List users
CommandResult HandleAclList(const protocol::Command& command,
                            CommandContext* context);

// ACL USERS - List users
CommandResult HandleAclUsers(const protocol::Command& command,
                             CommandContext* context);

// ACL CAT - Get command categories
CommandResult HandleAclCat(const protocol::Command& command,
                           CommandContext* context);

// ACL GENPASS - Generate password
CommandResult HandleAclGenpass(const protocol::Command& command,
                               CommandContext* context);

// ACL WHOAMI - Get current user
CommandResult HandleAclWhoami(const protocol::Command& command,
                              CommandContext* context);

// ACL SETUSER - Set user password
CommandResult HandleAclSetpass(const protocol::Command& command,
                               CommandContext* context);

// ACL GETUSER - Get user password
CommandResult HandleAclGetpass(const protocol::Command& command,
                               CommandContext* context);

}  // namespace astra::commands
