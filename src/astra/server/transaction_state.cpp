// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "transaction_state.hpp"

namespace astra::server {

const absl::flat_hash_set<std::string> TransactionStateStore::kEmptyWatchedKeys =
    {};

bool TransactionStateStore::IsInTransaction(uint64_t connection_id) const {
  auto it = states_.find(connection_id);
  return it != states_.end() && it->second.in_transaction;
}

void TransactionStateStore::BeginTransaction(uint64_t connection_id) {
  auto& state = states_[connection_id];
  state.in_transaction = true;
  state.queued_commands.clear();
}

void TransactionStateStore::QueueCommand(uint64_t connection_id,
                                         const protocol::Command& cmd) {
  states_[connection_id].queued_commands.push_back(cmd);
}

absl::InlinedVector<protocol::Command, 16>
TransactionStateStore::GetQueuedCommands(uint64_t connection_id) const {
  auto it = states_.find(connection_id);
  if (it == states_.end()) {
    return {};
  }
  return it->second.queued_commands;
}

void TransactionStateStore::ClearQueuedCommands(uint64_t connection_id) {
  auto it = states_.find(connection_id);
  if (it == states_.end()) {
    return;
  }
  it->second.queued_commands.clear();
  MaybeErase(connection_id);
}

void TransactionStateStore::DiscardTransaction(uint64_t connection_id) {
  auto it = states_.find(connection_id);
  if (it == states_.end()) {
    return;
  }
  it->second.in_transaction = false;
  it->second.queued_commands.clear();
  it->second.watched_keys.clear();
  it->second.watched_key_versions.clear();
  states_.erase(it);
}

void TransactionStateStore::WatchKey(uint64_t connection_id,
                                     const std::string& key, uint64_t version) {
  auto& state = states_[connection_id];
  state.watched_keys.insert(key);
  state.watched_key_versions[key] = version;
}

const absl::flat_hash_set<std::string>& TransactionStateStore::GetWatchedKeys(
    uint64_t connection_id) const {
  auto it = states_.find(connection_id);
  if (it == states_.end()) {
    return kEmptyWatchedKeys;
  }
  return it->second.watched_keys;
}

bool TransactionStateStore::IsWatchedKeyModified(
    uint64_t connection_id,
    const absl::AnyInvocable<uint64_t(const std::string&) const>& get_version)
    const {
  auto it = states_.find(connection_id);
  if (it == states_.end()) {
    return false;
  }
  const auto& state = it->second;
  for (const auto& key : state.watched_keys) {
    auto vit = state.watched_key_versions.find(key);
    if (vit == state.watched_key_versions.end()) {
      continue;
    }
    if (get_version(key) != vit->second) {
      return true;
    }
  }
  return false;
}

void TransactionStateStore::ClearWatchedKeys(uint64_t connection_id) {
  auto it = states_.find(connection_id);
  if (it == states_.end()) {
    return;
  }
  it->second.watched_keys.clear();
  it->second.watched_key_versions.clear();
  MaybeErase(connection_id);
}

void TransactionStateStore::ClearConnectionState(uint64_t connection_id) {
  states_.erase(connection_id);
}

void TransactionStateStore::MaybeErase(uint64_t connection_id) {
  auto it = states_.find(connection_id);
  if (it == states_.end()) {
    return;
  }
  if (!it->second.in_transaction && it->second.queued_commands.empty() &&
      it->second.watched_keys.empty()) {
    states_.erase(it);
  }
}

}  // namespace astra::server
