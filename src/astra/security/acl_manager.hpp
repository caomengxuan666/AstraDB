// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/synchronization/mutex.h>

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

#include "astra/base/logging.hpp"

namespace astra::security {

// User ACL permissions
enum class AclPermission { kRead = 1 << 0, kWrite = 1 << 1, kAdmin = 1 << 2 };

// User info
struct AclUser {
  std::string username;
  bool enabled = true;
  bool all_keys = false;     // allkeys flag
  bool all_channels = false; // allchannels flag
  bool no_password = false;  // nopass flag
  
  // Passwords (multiple passwords supported)
  std::vector<std::string> passwords;  // SHA256 hashed passwords
  
  // Command permissions (categories and individual commands)
  std::vector<std::string> categories; // e.g., "+@all", "+@string"
  std::vector<std::string> commands;   // e.g., "+GET", "-SET"
  
  // Key patterns (glob style)
  std::vector<std::string> key_patterns;  // e.g., "~*", "~objects:*"
  
  // Channel patterns (glob style)
  std::vector<std::string> channel_patterns;  // e.g., "&*", "&chatroom:*"
  
  // Convert to ACL LIST format
  std::string ToAclListString() const {
    std::string result = "user " + username + " ";
    
    if (!enabled) {
      result += "off ";
    } else {
      result += "on ";
    }
    
    if (no_password) {
      result += "nopass ";
    }
    
    if (all_keys) {
      result += "~* ";
    } else {
      for (const auto& pattern : key_patterns) {
        result += pattern + " ";
      }
    }
    
    if (all_channels) {
      result += "&* ";
    } else {
      for (const auto& pattern : channel_patterns) {
        result += pattern + " ";
      }
    }
    
    for (const auto& category : categories) {
      result += category + " ";
    }
    
    for (const auto& command : commands) {
      result += command + " ";
    }
    
    return result;
  }
};

// ACL Manager
class AclManager {
 public:
  AclManager() noexcept = default;
  ~AclManager() noexcept = default;

  // Non-copyable, non-movable
  AclManager(const AclManager&) = delete;
  AclManager& operator=(const AclManager&) = delete;
  AclManager(bool) = delete;
  AclManager& operator=(bool) = delete;

  // Initialize ACL
  bool Init() noexcept {
    // Create default user with all permissions
    AclUser default_user;
    default_user.username = "default";
    default_user.enabled = true;
    default_user.all_keys = true;
    default_user.all_channels = true;
    default_user.no_password = true;
    default_user.categories.push_back("+@all");
    default_user.commands.push_back("+@all");

    users_["default"] = default_user;
    initialized_.store(true, std::memory_order_release);
    return true;
  }

  // Authenticate user
  bool Authenticate(const std::string& username,
                    const std::string& password) noexcept {
    absl::MutexLock lock(&mutex_);
    auto it = users_.find(username);
    if (it == users_.end()) {
      return false;
    }

    if (!it->second.enabled) {
      return false;
    }

    // If no password required, any password works
    if (it->second.no_password) {
      return true;
    }

    // Check against all configured passwords
    // In production, passwords should be SHA256 hashed
    for (const auto& pwd : it->second.passwords) {
      if (pwd == password) {  // Simple comparison for now
        return true;
      }
    }

    return false;
  }

  // Create or update user with ACL rules
  bool SetUser(const std::string& username, 
               const std::vector<std::string>& rules) noexcept {
    absl::MutexLock lock(&mutex_);
    
    AclUser* user = nullptr;
    auto it = users_.find(username);
    
    // Check if user exists
    if (it == users_.end()) {
      // Create new user
      AclUser new_user;
      new_user.username = username;
      new_user.enabled = false;
      users_[username] = new_user;
      user = &users_[username];
      ASTRADB_LOG_INFO("Created user: {}", username);
    } else {
      user = &it->second;
    }
    
    // Process rules
    for (const auto& rule : rules) {
      if (rule == "on") {
        user->enabled = true;
      } else if (rule == "off") {
        user->enabled = false;
      } else if (rule == "nopass") {
        user->no_password = true;
        user->passwords.clear();
      } else if (rule == "allkeys") {
        user->all_keys = true;
        user->key_patterns.clear();
      } else if (rule == "allchannels") {
        user->all_channels = true;
        user->channel_patterns.clear();
      } else if (rule == "reset") {
        // Reset user to default state
        user->enabled = false;
        user->all_keys = false;
        user->all_channels = false;
        user->no_password = false;
        user->passwords.clear();
        user->categories.clear();
        user->commands.clear();
        user->key_patterns.clear();
        user->channel_patterns.clear();
      } else if (rule == "resetkeys") {
        user->all_keys = false;
        user->key_patterns.clear();
      } else if (rule == "resetchannels") {
        user->all_channels = false;
        user->channel_patterns.clear();
      } else if (rule == "resetpass") {
        user->no_password = false;
        user->passwords.clear();
      } else if (rule == "allcommands" || rule == "+@all") {
        user->commands.clear();
        user->commands.push_back("+@all");
        user->categories.push_back("+@all");
      } else if (rule == "nocommands" || rule == "-@all") {
        user->commands.clear();
        user->categories.clear();
        user->commands.push_back("-@all");
      } else if (rule.size() > 1) {
        char prefix = rule[0];
        std::string suffix = rule.substr(1);
        
        if (prefix == '+' || prefix == '-') {
          // Command or category
          user->commands.push_back(rule);
          if (suffix.size() > 0 && suffix[0] == '@') {
            user->categories.push_back(rule);
          }
        } else if (prefix == '>') {
          // Add password (clear text, should be hashed)
          // For now, store as-is
          user->no_password = false;
          user->passwords.push_back(suffix);
        } else if (prefix == '#') {
          // Add password (already hashed)
          user->no_password = false;
          user->passwords.push_back(suffix);
        } else if (prefix == '<') {
          // Remove password
          auto it = std::find(user->passwords.begin(), user->passwords.end(), suffix);
          if (it != user->passwords.end()) {
            user->passwords.erase(it);
          }
        } else if (prefix == '~') {
          // Key pattern
          if (suffix == "*") {
            user->all_keys = true;
            user->key_patterns.clear();
          } else {
            user->key_patterns.push_back(rule);
          }
        } else if (prefix == '&') {
          // Channel pattern
          if (suffix == "*") {
            user->all_channels = true;
            user->channel_patterns.clear();
          } else {
            user->channel_patterns.push_back(rule);
          }
        }
      }
    }
    
    return true;
  }

  // Create user (backward compatibility)
  bool CreateUser(const std::string& username, const std::string& password,
                  uint32_t permissions = 0, bool enabled = true) noexcept {
    std::vector<std::string> rules;
    rules.push_back(enabled ? "on" : "off");
    if (!password.empty()) {
      rules.push_back(">" + password);
    } else {
      rules.push_back("nopass");
    }
    
    if (permissions != 0) {
      rules.push_back("+@all");
    }
    
    return SetUser(username, rules);
  }

  // Delete user
  bool DeleteUser(const std::string& username) noexcept {
    absl::MutexLock lock(&mutex_);
    auto it = users_.find(username);
    if (it == users_.end()) {
      return false;
    }
    if (username == "default") {
      return false;  // Cannot delete default user
    }
    users_.erase(it);
    ASTRADB_LOG_INFO("Deleted user: {}", username);
    return true;
  }

  // Get user info
  const AclUser* GetUserInfo(const std::string& username) const noexcept {
    absl::MutexLock lock(&mutex_);
    auto it = users_.find(username);
    if (it == users_.end()) {
      return nullptr;
    }
    return &it->second;
  }

  // Set user password (backward compatibility)
  bool SetPassword(const std::string& username,
                   const std::string& password) noexcept {
    std::vector<std::string> rules;
    if (password.empty()) {
      rules.push_back("nopass");
    } else {
      rules.push_back(">" + password);
    }
    return SetUser(username, rules);
  }

  // Get users in ACL LIST format
  std::vector<std::string> GetUsers() const noexcept {
    absl::MutexLock lock(&mutex_);
    std::vector<std::string> users;
    for (const auto& [name, user] : users_) {
      users.push_back(user.ToAclListString());
    }
    return users;
  }
  
  // Get user names only
  std::vector<std::string> GetUserNames() const noexcept {
    absl::MutexLock lock(&mutex_);
    std::vector<std::string> users;
    for (const auto& [name, user] : users_) {
      users.push_back(name);
    }
    return users;
  }

  // Check if user has permission to execute a command
  bool CheckCommandPermission(const std::string& username,
                               const std::string& command) const noexcept {
    absl::MutexLock lock(&mutex_);
    auto it = users_.find(username);
    if (it == users_.end()) {
      ASTRADB_LOG_WARN("CheckCommandPermission: User '{}' doesn't exist", username);
      return false;  // User doesn't exist
    }
    
    const AclUser& user = it->second;
    if (!user.enabled) {
      ASTRADB_LOG_WARN("CheckCommandPermission: User '{}' is disabled", username);
      return false;  // User is disabled
    }
    
    // Check if user has -@all (no commands)
    for (const auto& cmd : user.commands) {
      if (cmd == "-@all") {
        ASTRADB_LOG_WARN("CheckCommandPermission: User '{}' has -@all", username);
        return false;
      }
    }
    
    // Check if user has +@all (all commands)
    for (const auto& cmd : user.commands) {
      if (cmd == "+@all" || cmd == "allcommands") {
        ASTRADB_LOG_INFO("CheckCommandPermission: User '{}' has +@all, allowing command '{}'", username, command);
        return true;
      }
    }
    
    // Check if user has specific command permission
    std::string cmd_plus = "+" + command;
    for (const auto& cmd : user.commands) {
      if (cmd == cmd_plus) {
        ASTRADB_LOG_INFO("CheckCommandPermission: User '{}' has +{}, allowing", username, command);
        return true;
      }
    }
    
    // Check if user has category permission
    // For now, we assume all commands belong to @all category
    // In production, this should check actual command categories
    for (const auto& cat : user.categories) {
      if (cat == "+@all" || cat == "allcommands") {
        ASTRADB_LOG_INFO("CheckCommandPermission: User '{}' has category +@all, allowing command '{}'", username, command);
        return true;
      }
    }
    
    // Default: deny
    ASTRADB_LOG_WARN("CheckCommandPermission: User '{}' doesn't have permission for command '{}'", username, command);
    return false;
  }

  // Check if user has permission to access a key
  bool CheckKeyPermission(const std::string& username,
                           const std::string& key) const noexcept {
    absl::MutexLock lock(&mutex_);
    auto it = users_.find(username);
    if (it == users_.end()) {
      return false;  // User doesn't exist
    }
    
    const AclUser& user = it->second;
    if (!user.enabled) {
      return false;  // User is disabled
    }
    
    // If user has allkeys permission, allow access
    if (user.all_keys) {
      return true;
    }
    
    // Check if key matches any pattern
    for (const auto& pattern : user.key_patterns) {
      if (pattern.size() > 1 && pattern[0] == '~') {
        std::string pattern_str = pattern.substr(1);
        // Simple glob matching (supports * only)
        if (pattern_str == "*") {
          return true;
        } else if (pattern_str.find('*') != std::string::npos) {
          // Pattern contains wildcard
          size_t pos = pattern_str.find('*');
          std::string prefix = pattern_str.substr(0, pos);
          std::string suffix = pattern_str.substr(pos + 1);
          if (key.size() >= prefix.size() + suffix.size() &&
              key.substr(0, prefix.size()) == prefix &&
              key.substr(key.size() - suffix.size()) == suffix) {
            return true;
          }
        } else {
          // Exact match
          if (key == pattern_str) {
            return true;
          }
        }
      }
    }
    
    return false;
  }

 private:
  mutable absl::Mutex mutex_;
  absl::flat_hash_map<std::string, AclUser> users_;
  std::atomic<bool> initialized_{false};
};

}  // namespace astra::security
