// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "astra/commands/database.hpp"
#include "astra/commands/command_handler.hpp"
#include "astra/base/logging.hpp"
#include "astra/core/async/thread_pool.hpp"
#include "astra/persistence/leveldb_adapter.hpp"
#include <memory>
#include <string>

namespace astra::server {

// Shard - A single database shard (data only, no worker thread)
class Shard {
 public:
  explicit Shard(int shard_id, size_t io_context_index = 0, size_t num_databases = 16);
  ~Shard() = default;
  
  int GetShardId() const { return shard_id_; }
  
  // Get database by index (for multi-database support)
  commands::Database* GetDatabase(int db_index = 0) {
    return db_manager_->GetDatabase(db_index);
  }
  
  // Get database manager
  commands::DatabaseManager* GetDatabaseManager() { return db_manager_.get(); }
  
  // Get the shard's IO context index (for routing to thread pool)
  size_t GetIOContextIndex() const { return io_context_index_; }
  
  // Post work directly to main thread's io_context (simplest implementation)
  template <typename F>
  void Post(F&& work) {
    // Direct posting to main thread's io_context - no thread pool overhead
    astra::core::async::PostToMainIOContext(std::forward<F>(work));
  }
  
 private:
  int shard_id_;
  size_t io_context_index_;
  std::unique_ptr<commands::DatabaseManager> db_manager_;
};

// LocalShardManager - Manages local database shards (renamed to avoid conflict with cluster::ShardManager)
class LocalShardManager {
 public:
  explicit LocalShardManager(size_t num_shards = 16, size_t num_databases = 16);
  ~LocalShardManager() = default;
  
  // Get shard by key (consistent hashing)
  Shard* GetShard(const std::string& key);
  
  // Get shard by index
  Shard* GetShardByIndex(size_t index);
  
  // Get number of shards
  size_t GetShardCount() const { return shards_.size(); }
  
 private:
  std::vector<std::unique_ptr<Shard>> shards_;
};

}  // namespace astra::server