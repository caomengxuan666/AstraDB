// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <string>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/inlined_vector.h>
#include <absl/functional/any_invocable.h>

#include "astra/protocol/resp/resp_types.hpp"

namespace astra::server {

// Per-connection transaction state holder used by WorkerCommandContext.
class TransactionStateStore {
 public:
  bool IsInTransaction(uint64_t connection_id) const;
  void BeginTransaction(uint64_t connection_id);
  void QueueCommand(uint64_t connection_id, const protocol::Command& cmd);
  absl::InlinedVector<protocol::Command, 16> GetQueuedCommands(
      uint64_t connection_id) const;
  void ClearQueuedCommands(uint64_t connection_id);
  void DiscardTransaction(uint64_t connection_id);

  void WatchKey(uint64_t connection_id, const std::string& key,
                uint64_t version);
  const absl::flat_hash_set<std::string>& GetWatchedKeys(
      uint64_t connection_id) const;
  bool IsWatchedKeyModified(
      uint64_t connection_id,
      const absl::AnyInvocable<uint64_t(const std::string&) const>& get_version)
      const;
  void ClearWatchedKeys(uint64_t connection_id);

  void ClearConnectionState(uint64_t connection_id);

 private:
  struct TransactionState {
    bool in_transaction = false;
    absl::InlinedVector<protocol::Command, 16> queued_commands;
    absl::flat_hash_set<std::string> watched_keys;
    absl::flat_hash_map<std::string, uint64_t> watched_key_versions;
  };

  void MaybeErase(uint64_t connection_id);

  static const absl::flat_hash_set<std::string> kEmptyWatchedKeys;
  absl::flat_hash_map<uint64_t, TransactionState> states_;
};

}  // namespace astra::server

