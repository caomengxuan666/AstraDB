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
  explicit Shard(int shard_id, size_t io_context_index = 0);
  ~Shard() = default;
  
  int GetShardId() const { return shard_id_; }
  
  commands::Database* GetDatabase() { return &database_; }
  
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
  astra::commands::Database database_;
};

// LocalShardManager - Manages local database shards (renamed to avoid conflict with cluster::ShardManager)
class LocalShardManager {
 public:
  explicit LocalShardManager(size_t num_shards = 16);
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