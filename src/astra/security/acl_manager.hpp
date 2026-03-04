// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>

#include "astra/base/logging.hpp"

namespace astra::security {

// User ACL permissions
enum class AclPermission {
  kRead = 1 << 0,
  kWrite = 1 << 1,
 kAdmin = 1 << 2
};

// User info
struct AclUser {
  std::string username;
  std::string password;  // Hashed password
  bool enabled = true;
  uint32_t permissions = 0;
  
  // Password categories (key patterns with specific permissions)
  struct PasswordCategory {
    std::string pattern;
    uint32_t permissions;
  };
  std::vector<PasswordCategory> categories;
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
    default_user.password = "";
    default_user.permissions = static_cast<uint32_t>(AclPermission::kRead) | 
                              static_cast<uint32_t>(AclPermission::kWrite) | 
                              static_cast<uint32_t>(AclPermission::kAdmin);
    
    users_["default"] = default_user;
    initialized_.store(true, std::memory_order_release);
    return true;
  }
  
  // Authenticate user
  bool Authenticate(const std::string& username, const std::string& password) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(username);
    if (it == users_.end()) {
      return false;
    }
    
    if (!it->second.enabled) {
      return false;
    }
    
    // In production, use proper password hashing
    // For now, simple comparison
    return it->second.password == password;
  }
  
  // Create user
  bool CreateUser(const std::string& username, const std::string& password, 
                  uint32_t permissions = 0, bool enabled = true) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (users_.find(username) != users_.end()) {
      return false;
    }
    
    AclUser user;
    user.username = username;
    user.password = password;
    user.permissions = permissions;
    user.enabled = enabled;
    
    users_[username] = user;
    ASTRADB_LOG_INFO("Created user: {}", username);
    return true;
  }
  
  // Check user permission
  bool CheckPermission(const std::string& username, AclPermission perm) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(username);
    if (it == users_.end()) {
      return false;
    }
    return (it->second.permissions & static_cast<uint32_t>(perm)) != 0;
  }
  
  // Get user info
  std::vector<std::string> GetUsers() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> users;
    for (const auto& [name, user] : users_) {
      users.push_back(name);
    }
    return users;
  }
  
 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, AclUser> users_;
  std::atomic<bool> initialized_{false};
};

}  // namespace astra::security