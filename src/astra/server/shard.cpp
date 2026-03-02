// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "shard.hpp"
#include <absl/hash/hash.h>

namespace astra::server {

Shard::Shard(int shard_id, size_t io_context_index)
    : shard_id_(shard_id), io_context_index_(io_context_index) {
  ASTRADB_LOG_INFO("Shard {} created (IO context index: {})", shard_id, io_context_index);
}

ShardManager::ShardManager(size_t num_shards) {
  auto& pool = astra::core::async::GetGlobalThreadPool();
  size_t pool_size = pool.Size();
  
  shards_.reserve(num_shards);
  for (size_t i = 0; i < num_shards; ++i) {
    // Distribute shards across IO contexts using round-robin
    size_t io_context_index = i % pool_size;
    shards_.push_back(std::make_unique<Shard>(static_cast<int>(i), io_context_index));
  }
  ASTRADB_LOG_INFO("ShardManager created with {} shards (distributed across {} IO contexts)",
                  num_shards, pool_size);
}

Shard* ShardManager::GetShard(const std::string& key) {
  size_t hash_value = absl::Hash<std::string>{}(key);
  size_t shard_index = hash_value % shards_.size();
  return shards_[shard_index].get();
}

Shard* ShardManager::GetShardByIndex(size_t index) {
  if (index >= shards_.size()) {
    return nullptr;
  }
  return shards_[index].get();
}

}  // namespace astra::server