// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <absl/time/time.h>
#include <absl/time/clock.h>
#include <absl/container/flat_hash_map.h>

namespace astra::commands {

// Stream entry ID: <millisecondsTime>-<sequenceNumber>
struct StreamId {
  uint64_t ms = 0;
  uint64_t seq = 0;

  StreamId() = default;
  explicit StreamId(uint64_t m, uint64_t s) : ms(m), seq(s) {}
  explicit StreamId(const std::string& str);  // Parse from "ms-seq" format

  std::string ToString() const;
  bool operator<(const StreamId& other) const;
  bool operator<=(const StreamId& other) const;
  bool operator>(const StreamId& other) const { return other < *this; }
  bool operator>=(const StreamId& other) const { return !(other < *this); }
  bool operator==(const StreamId& other) const;
};

// Stream entry: ID + field-value pairs
struct StreamEntry {
  StreamId id;
  std::vector<std::pair<std::string, std::string>> fields;  // field-value pairs
};

// Pending entry with consumer tracking
struct PendingEntry {
  StreamEntry entry;
  std::string consumer;
  int64_t delivery_count = 0;
  absl::Time last_delivered;
};

// Consumer group state
struct StreamConsumerGroup {
  std::string name;
  StreamId last_delivered_id;  // Last ID delivered to this group
  absl::flat_hash_map<std::string, StreamId> consumers;  // consumer name -> last seen
  std::map<StreamId, PendingEntry> pending_entries;  // PEL: pending entries list
};

// Stream data structure
struct StreamData {
  std::deque<StreamEntry> entries;
  uint64_t last_generated_ms = 0;
  uint64_t last_generated_seq = 0;
  StreamId last_id;  // Last generated ID
  absl::flat_hash_map<std::string, StreamConsumerGroup> groups;
  size_t max_len = 0;  // MAXLEN, 0 = unlimited

  // Add entry, returns the generated ID
  StreamId AddEntry(const std::vector<std::pair<std::string, std::string>>& fields,
                    const std::string& id_spec, uint64_t max_len_val) {
    StreamId id;

    if (id_spec == "*") {
      // Auto-generate ID based on current time
      auto now = absl::Now();
      int64_t ms = absl::ToUnixMillis(now);

      if (ms < static_cast<int64_t>(last_generated_ms)) {
        ms = static_cast<int64_t>(last_generated_ms);
      }

      if (ms == static_cast<int64_t>(last_generated_ms)) {
        id.ms = last_generated_ms;
        id.seq = last_generated_seq + 1;
      } else {
        id.ms = static_cast<uint64_t>(ms);
        id.seq = 0;
      }
    } else {
      // Parse the provided ID
      StreamId parsed_id(id_spec);
      id = parsed_id;

      // Validate: must be greater than last
      if (!entries.empty()) {
        const auto& last = entries.back().id;
        if (id <= last) {
          return StreamId(0, 0);  // Invalid ID
        }
      }
    }

    last_generated_ms = id.ms;
    last_generated_seq = id.seq;

    StreamEntry entry;
    entry.id = id;
    entry.fields = fields;
    entries.push_back(entry);

    // Trim if needed
    while (max_len_val > 0 && entries.size() > max_len_val) {
      entries.pop_front();
    }

    this->max_len = max_len_val;
    return id;
  }

  // Read entries from a given ID
  std::vector<StreamEntry> Read(const StreamId& start, size_t count, bool exclusive = true) const {
    std::vector<StreamEntry> result;
    for (const auto& entry : entries) {
      if (exclusive) {
        if (entry.id <= start) continue;
      } else {
        if (entry.id < start) continue;
      }
      result.push_back(entry);
      if (result.size() >= count) break;
    }
    return result;
  }

  // Create consumer group
  bool CreateGroup(const std::string& group_name, const StreamId& start_id) {
    if (groups.contains(group_name)) {
      return false;
    }
    StreamConsumerGroup group;
    group.name = group_name;
    group.last_delivered_id = start_id;
    groups[group_name] = std::move(group);
    return true;
  }

  // Read for consumer group
  std::vector<StreamEntry> ReadGroup(const std::string& group_name,
                                     const std::string& consumer_name,
                                     size_t count) {
    auto it = groups.find(group_name);
    if (it == groups.end()) {
      return {};
    }

    auto& group = it->second;
    std::vector<StreamEntry> result;

    for (const auto& entry : entries) {
      if (entry.id <= group.last_delivered_id) continue;
      result.push_back(entry);
      
      // Create pending entry
      PendingEntry pending;
      pending.entry = entry;
      pending.consumer = consumer_name;
      pending.delivery_count = 1;
      pending.last_delivered = absl::Now();
      group.pending_entries[entry.id] = pending;
      
      if (result.size() >= count) break;
    }

    if (!result.empty()) {
      group.last_delivered_id = result.back().id;
      group.consumers[consumer_name] = result.back().id;
    }

    return result;
  }
};

}  // namespace astra::commands
