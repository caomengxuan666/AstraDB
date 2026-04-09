// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "stream_commands.hpp"

#include <absl/strings/numbers.h>
#include <absl/strings/str_split.h>
#include <absl/time/time.h>

#include "astra/base/logging.hpp"
#include "astra/container/stream_data.hpp"
#include "command_auto_register.hpp"
#include "database.hpp"

namespace astra::commands {

// Helper function to convert stream entry to RESP value
protocol::RespValue StreamEntryToResp(
    const StreamId& id,
    const std::vector<std::pair<std::string, std::string>>& fields) {
  std::vector<protocol::RespValue> entry_array;

  // Add ID
  protocol::RespValue id_val;
  id_val.SetString(id.ToString(), protocol::RespType::kBulkString);
  entry_array.push_back(id_val);

  // Add fields
  std::vector<protocol::RespValue> fields_array;
  for (const auto& [field, value] : fields) {
    protocol::RespValue f, v;
    f.SetString(field, protocol::RespType::kBulkString);
    v.SetString(value, protocol::RespType::kBulkString);
    fields_array.push_back(f);
    fields_array.push_back(v);
  }
  protocol::RespValue fields_val;
  fields_val.SetArray(std::move(fields_array));
  entry_array.push_back(fields_val);

  protocol::RespValue entry_val;
  entry_val.SetArray(std::move(entry_array));
  return entry_val;
}

// ============== StreamId Implementation ==============

StreamId::StreamId(const std::string& str) {
  if (str == "*") {
    // Will be auto-generated
    ms = 0;
    seq = 0;
    return;
  }

  std::vector<std::string> parts = absl::StrSplit(str, '-');
  if (parts.size() == 2) {
    (void)absl::SimpleAtoi(parts[0], reinterpret_cast<int64_t*>(&ms));
    (void)absl::SimpleAtoi(parts[1], reinterpret_cast<int64_t*>(&seq));
  } else if (parts.size() == 1) {
    (void)absl::SimpleAtoi(parts[0], reinterpret_cast<int64_t*>(&ms));
    seq = 0;
  }
}

std::string StreamId::ToString() const { return absl::StrCat(ms, "-", seq); }

bool StreamId::operator<(const StreamId& other) const {
  if (ms != other.ms) return ms < other.ms;
  return seq < other.seq;
}

bool StreamId::operator<=(const StreamId& other) const {
  return !(other < *this);
}

bool StreamId::operator==(const StreamId& other) const {
  return ms == other.ms && seq == other.seq;
}

// ============== Command Handlers ==============

CommandResult HandleXAdd(const protocol::Command& command,
                         CommandContext* context) {
  // XADD key [MAXLEN ~ count] ID field value [field value ...]
  if (command.ArgCount() < 4) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'xadd' command");
  }

  const std::string& key = command[0].AsString();
  size_t arg_idx = 1;
  uint64_t max_len = 0;

  // Parse MAXLEN option
  if (command.ArgCount() > arg_idx + 2 &&
      command[arg_idx].AsString() == "MAXLEN") {
    arg_idx++;  // Skip MAXLEN
    if (command[arg_idx].AsString() == "~") {
      arg_idx++;  // Skip ~ (approximate)
    }
    max_len = std::stoull(command[arg_idx].AsString());
    arg_idx++;
  }

  // Get ID
  const std::string& id_spec = command[arg_idx].AsString();
  arg_idx++;

  // Parse field-value pairs
  std::vector<std::pair<std::string, std::string>> fields;
  while (arg_idx + 1 < command.ArgCount()) {
    fields.emplace_back(command[arg_idx].AsString(),
                        command[arg_idx + 1].AsString());
    arg_idx += 2;
  }

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  // Get or create stream
  StreamData* stream = db->GetOrCreateStream(key);
  if (!stream) {
    return CommandResult(false, "ERR cannot create stream");
  }

  StreamId id = stream->AddEntry(fields, id_spec, max_len);
  if (id.ms == 0 && id.seq == 0 && id_spec != "*") {
    return CommandResult(false,
                         "ERR The ID specified in XADD is equal or smaller "
                         "than the target stream top item");
  }

  protocol::RespValue response;
  response.SetString(id.ToString(), protocol::RespType::kBulkString);
  return CommandResult(response);
}

CommandResult HandleXRead(const protocol::Command& command,
                          CommandContext* context) {
  // XREAD [COUNT count] STREAMS key ID [key ID ...]
  size_t arg_idx = 0;
  size_t count = 1;

  if (command.ArgCount() < 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'xread' command");
  }

  // Parse options
  while (arg_idx < command.ArgCount() &&
         command[arg_idx].AsString() == "COUNT") {
    arg_idx++;
    count = std::stoull(command[arg_idx].AsString());
    arg_idx++;
  }

  if (command[arg_idx].AsString() != "STREAMS") {
    return CommandResult(false, "ERR syntax error");
  }
  arg_idx++;  // Skip STREAMS

  // Parse keys and IDs
  std::vector<std::string> keys;
  std::vector<StreamId> ids;

  size_t remaining = command.ArgCount() - arg_idx;
  if (remaining % 2 != 0) {
    return CommandResult(false,
                         "ERR Unbalanced XREAD list of streams: for each "
                         "stream key an ID or '$' must be specified");
  }

  size_t num_streams = remaining / 2;
  for (size_t i = 0; i < num_streams; ++i) {
    keys.push_back(command[arg_idx + i].AsString());
  }
  arg_idx += num_streams;
  for (size_t i = 0; i < num_streams; ++i) {
    std::string id_str = command[arg_idx + i].AsString();
    ids.push_back(StreamId(
        id_str == "$" ? "18446744073709551615-18446744073709551615" : id_str));
  }

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  // Read from each stream
  std::vector<protocol::RespValue> stream_results;
  for (size_t i = 0; i < keys.size(); ++i) {
    StreamData* stream = db->GetStream(keys[i]);
    if (!stream) continue;

    auto entries = stream->Read(ids[i], count);
    if (entries.empty()) continue;

    // Build result for this stream: [key, [[id, [field, value, ...]], ...]]
    std::vector<protocol::RespValue> stream_result;
    protocol::RespValue key_val;
    key_val.SetString(keys[i], protocol::RespType::kBulkString);
    stream_result.push_back(key_val);

    std::vector<protocol::RespValue> entries_array;
    for (const auto& entry : entries) {
      std::vector<protocol::RespValue> entry_array;
      protocol::RespValue id_val;
      id_val.SetString(entry.id.ToString(), protocol::RespType::kBulkString);
      entry_array.push_back(id_val);

      std::vector<protocol::RespValue> fields_array;
      for (const auto& [field, value] : entry.fields) {
        protocol::RespValue f, v;
        f.SetString(field, protocol::RespType::kBulkString);
        v.SetString(value, protocol::RespType::kBulkString);
        fields_array.push_back(f);
        fields_array.push_back(v);
      }
      protocol::RespValue fields_val;
      fields_val.SetArray(std::move(fields_array));
      entry_array.push_back(fields_val);

      protocol::RespValue entry_val;
      entry_val.SetArray(std::move(entry_array));
      entries_array.push_back(entry_val);
    }

    protocol::RespValue entries_val;
    entries_val.SetArray(std::move(entries_array));
    stream_result.push_back(entries_val);

    protocol::RespValue stream_val;
    stream_val.SetArray(std::move(stream_result));
    stream_results.push_back(stream_val);
  }

  protocol::RespValue response;
  if (stream_results.empty()) {
    response = protocol::RespValue(protocol::RespType::kNull);
  } else {
    response.SetArray(std::move(stream_results));
  }
  return CommandResult(response);
}

CommandResult HandleXRange(const protocol::Command& command,
                           CommandContext* context) {
  // XRANGE key start end [COUNT count]
  if (command.ArgCount() < 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'xrange' command");
  }

  const std::string& key = command[0].AsString();
  const std::string& start_str = command[1].AsString();
  const std::string& end_str = command[2].AsString();

  // Handle special IDs
  StreamId start;
  StreamId end;

  if (start_str == "-") {
    start = StreamId(0, 0);
  } else {
    start = StreamId(start_str);
  }

  if (end_str == "+") {
    end = StreamId(UINT64_MAX, UINT64_MAX);
  } else {
    end = StreamId(end_str);
  }

  size_t count = 0;
  if (command.ArgCount() > 4 && command[3].AsString() == "COUNT") {
    count = std::stoull(command[4].AsString());
  }

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  StreamData* stream = db->GetStream(key);
  if (!stream) {
    protocol::RespValue response;
    response.SetArray({});
    return CommandResult(response);
  }

  std::vector<protocol::RespValue> result;
  size_t found = 0;

  for (const auto& entry : stream->entries) {
    if (entry.id < start || entry.id > end) continue;
    if (count > 0 && found >= count) break;

    std::vector<protocol::RespValue> entry_array;
    protocol::RespValue id_val;
    id_val.SetString(entry.id.ToString(), protocol::RespType::kBulkString);
    entry_array.push_back(id_val);

    std::vector<protocol::RespValue> fields_array;
    for (const auto& [field, value] : entry.fields) {
      protocol::RespValue f, v;
      f.SetString(field, protocol::RespType::kBulkString);
      v.SetString(value, protocol::RespType::kBulkString);
      fields_array.push_back(f);
      fields_array.push_back(v);
    }

    protocol::RespValue fields_val;
    fields_val.SetArray(std::move(fields_array));
    entry_array.push_back(fields_val);

    protocol::RespValue entry_val;
    entry_val.SetArray(std::move(entry_array));
    result.push_back(entry_val);
    found++;
  }

  protocol::RespValue response;
  response.SetArray(std::move(result));
  return CommandResult(response);
}

CommandResult HandleXLen(const protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'xlen' command");
  }

  const std::string& key = command[0].AsString();
  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  StreamData* stream = db->GetStream(key);
  int64_t len = stream ? static_cast<int64_t>(stream->entries.size()) : 0;

  protocol::RespValue response;
  response.SetInteger(len);
  return CommandResult(response);
}

CommandResult HandleXDel(const protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'xdel' command");
  }

  const std::string& key = command[0].AsString();
  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  StreamData* stream = db->GetStream(key);
  if (!stream) {
    protocol::RespValue response;
    response.SetInteger(0);
    return CommandResult(response);
  }

  size_t deleted = 0;
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    StreamId id(command[i].AsString());
    auto it = std::find_if(stream->entries.begin(), stream->entries.end(),
                           [&id](const StreamEntry& e) { return e.id == id; });
    if (it != stream->entries.end()) {
      stream->entries.erase(it);
      deleted++;
    }
  }

  protocol::RespValue response;
  response.SetInteger(static_cast<int64_t>(deleted));
  return CommandResult(response);
}

CommandResult HandleXTrim(const protocol::Command& command,
                          CommandContext* context) {
  // XTRIM key MAXLEN [~] count
  if (command.ArgCount() < 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'xtrim' command");
  }

  const std::string& key = command[0].AsString();
  if (command[1].AsString() != "MAXLEN") {
    return CommandResult(false, "ERR syntax error");
  }

  size_t arg_idx = 2;
  if (command[arg_idx].AsString() == "~") {
    arg_idx++;
  }

  size_t max_len = std::stoull(command[arg_idx].AsString());

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  StreamData* stream = db->GetStream(key);
  if (!stream) {
    protocol::RespValue response;
    response.SetInteger(0);
    return CommandResult(response);
  }

  size_t trimmed = 0;
  while (stream->entries.size() > max_len) {
    stream->entries.pop_front();
    trimmed++;
  }

  protocol::RespValue response;
  response.SetInteger(static_cast<int64_t>(trimmed));
  return CommandResult(response);
}

CommandResult HandleXGroup(const protocol::Command& command,
                           CommandContext* context) {
  // XGROUP CREATE key groupname id [MKSTREAM]
  // XGROUP DESTROY key groupname
  // XGROUP SETID key groupname id
  if (command.ArgCount() < 4) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'xgroup' command");
  }

  const std::string& subcmd = command[0].AsString();
  const std::string& key = command[1].AsString();
  const std::string& group_name = command[2].AsString();

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  if (absl::AsciiStrToLower(subcmd) == "create") {
    StreamId start_id(command[3].AsString());
    bool mkstream =
        command.ArgCount() > 4 && command[4].AsString() == "MKSTREAM";

    StreamData* stream = db->GetStream(key);
    if (!stream && !mkstream) {
      return CommandResult(false, "ERR no such key");
    }

    if (!stream) {
      stream = db->GetOrCreateStream(key);
    }

    if (!stream->CreateGroup(group_name, start_id)) {
      return CommandResult(false,
                           "ERR BUSYGROUP Consumer Group name already exists");
    }

    protocol::RespValue response;
    response.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(response);
  } else if (absl::AsciiStrToLower(subcmd) == "destroy") {
    StreamData* stream = db->GetStream(key);
    if (!stream || !stream->groups.erase(group_name)) {
      protocol::RespValue response;
      response.SetInteger(0);
      return CommandResult(response);
    }

    protocol::RespValue response;
    response.SetInteger(1);
    return CommandResult(response);
  }

  return CommandResult(false, "ERR unknown subcommand");
}

CommandResult HandleXReadGroup(const protocol::Command& command,
                               CommandContext* context) {
  // XREADGROUP GROUP group consumer [COUNT count] STREAMS key ID [key ID ...]
  if (command.ArgCount() < 6 || command[0].AsString() != "GROUP") {
    return CommandResult(
        false, "ERR wrong number of arguments for 'xreadgroup' command");
  }

  const std::string& group_name = command[1].AsString();
  const std::string& consumer_name = command[2].AsString();
  size_t arg_idx = 3;
  size_t count = 1;

  while (arg_idx < command.ArgCount() &&
         command[arg_idx].AsString() == "COUNT") {
    arg_idx++;
    count = std::stoull(command[arg_idx].AsString());
    arg_idx++;
  }

  if (command[arg_idx].AsString() != "STREAMS") {
    return CommandResult(false, "ERR syntax error");
  }
  arg_idx++;

  size_t remaining = command.ArgCount() - arg_idx;
  if (remaining % 2 != 0) {
    return CommandResult(false, "ERR Unbalanced list");
  }

  std::vector<std::string> keys;
  for (size_t i = 0; i < remaining / 2; ++i) {
    keys.push_back(command[arg_idx + i].AsString());
  }

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  std::vector<protocol::RespValue> stream_results;
  for (const auto& key : keys) {
    StreamData* stream = db->GetStream(key);
    if (!stream) continue;

    auto entries = stream->ReadGroup(group_name, consumer_name, count);
    if (entries.empty()) continue;

    std::vector<protocol::RespValue> stream_result;
    protocol::RespValue key_val;
    key_val.SetString(key, protocol::RespType::kBulkString);
    stream_result.push_back(key_val);

    std::vector<protocol::RespValue> entries_array;
    for (const auto& entry : entries) {
      std::vector<protocol::RespValue> entry_array;
      protocol::RespValue id_val;
      id_val.SetString(entry.id.ToString(), protocol::RespType::kBulkString);
      entry_array.push_back(id_val);

      std::vector<protocol::RespValue> fields_array;
      for (const auto& [field, value] : entry.fields) {
        protocol::RespValue f, v;
        f.SetString(field, protocol::RespType::kBulkString);
        v.SetString(value, protocol::RespType::kBulkString);
        fields_array.push_back(f);
        fields_array.push_back(v);
      }
      protocol::RespValue fields_val;
      fields_val.SetArray(std::move(fields_array));
      entry_array.push_back(fields_val);

      protocol::RespValue entry_val;
      entry_val.SetArray(std::move(entry_array));
      entries_array.push_back(entry_val);
    }

    protocol::RespValue entries_val;
    entries_val.SetArray(std::move(entries_array));
    stream_result.push_back(entries_val);

    protocol::RespValue stream_val;
    stream_val.SetArray(std::move(stream_result));
    stream_results.push_back(stream_val);
  }

  protocol::RespValue response;
  if (stream_results.empty()) {
    response = protocol::RespValue(protocol::RespType::kNull);
  } else {
    response.SetArray(std::move(stream_results));
  }
  return CommandResult(response);
}

CommandResult HandleXAck(const protocol::Command& command,
                         CommandContext* context) {
  // XACK key group ID [ID ...]
  if (command.ArgCount() < 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'xack' command");
  }

  const std::string& key = command[0].AsString();
  const std::string& group_name = command[1].AsString();

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  StreamData* stream = db->GetStream(key);
  if (!stream) {
    protocol::RespValue response;
    response.SetInteger(0);
    return CommandResult(response);
  }

  auto group_it = stream->groups.find(group_name);
  if (group_it == stream->groups.end()) {
    protocol::RespValue response;
    response.SetInteger(0);
    return CommandResult(response);
  }

  size_t acked = 0;
  for (size_t i = 2; i < command.ArgCount(); ++i) {
    StreamId id(command[i].AsString());
    if (group_it->second.pending_entries.erase(id)) {
      acked++;
    }
  }

  protocol::RespValue response;
  response.SetInteger(static_cast<int64_t>(acked));
  return CommandResult(response);
}

CommandResult HandleXInfo(const protocol::Command& command,
                          CommandContext* context) {
  // XINFO STREAM key
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'xinfo' command");
  }

  const std::string& subcmd = command[0].AsString();
  const std::string& key = command[1].AsString();

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  if (absl::AsciiStrToLower(subcmd) == "stream") {
    StreamData* stream = db->GetStream(key);
    if (!stream) {
      return CommandResult(false, "ERR no such key");
    }

    // Return stream info
    std::vector<protocol::RespValue> result;
    auto add_field = [&](const std::string& name, int64_t value) {
      protocol::RespValue n, v;
      n.SetString(name, protocol::RespType::kBulkString);
      v.SetInteger(value);
      result.push_back(n);
      result.push_back(v);
    };

    add_field("length", static_cast<int64_t>(stream->entries.size()));
    add_field("radix-tree-keys", 0);
    add_field("radix-tree-nodes", 0);
    add_field("groups", static_cast<int64_t>(stream->groups.size()));
    add_field("last-generated-id", 0);

    protocol::RespValue response;
    response.SetArray(std::move(result));
    return CommandResult(response);
  }

  return CommandResult(false, "ERR unknown subcommand");
}

// XCLAIM key group consumer min-idle-time ID [ID ...] [IDLE ms] [TIME
// ms-unix-time] [RETRYCOUNT count] [FORCE] [JUSTID] Transfer ownership of
// pending stream entries to a different consumer
CommandResult HandleXClaim(const protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 5) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'XCLAIM' command");
  }

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  const std::string& key = command[0].AsString();
  const std::string& group = command[1].AsString();
  const std::string& consumer = command[2].AsString();
  int64_t min_idle_time;

  if (!absl::SimpleAtoi(command[3].AsString(), &min_idle_time)) {
    return CommandResult(false, "ERR invalid min-idle-time");
  }

  StreamData* stream = db->GetStream(key);
  if (!stream) {
    return CommandResult(false, "ERR no such key");
  }

  auto group_it = stream->groups.find(group);
  if (group_it == stream->groups.end()) {
    return CommandResult(false, "ERR no such group");
  }

  // Parse options and IDs
  std::vector<StreamId> ids;
  bool just_id = false;
  int64_t idle_time_override = -1;

  for (size_t i = 4; i < command.ArgCount(); ++i) {
    std::string arg = command[i].AsString();

    if (absl::AsciiStrToUpper(arg) == "JUSTID") {
      just_id = true;
    } else if (absl::AsciiStrToUpper(arg) == "IDLE") {
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR syntax error");
      }
      if (!absl::SimpleAtoi(command[++i].AsString(), &idle_time_override)) {
        return CommandResult(false, "ERR invalid idle time");
      }
    } else {
      // Treat as stream ID
      ids.push_back(StreamId(arg));
    }
  }

  std::vector<protocol::RespValue> result;
  absl::Time now = absl::Now();

  for (const auto& id : ids) {
    auto entry_it = group_it->second.pending_entries.find(id);
    if (entry_it != group_it->second.pending_entries.end()) {
      // Check idle time
      auto idle_duration = now - entry_it->second.last_delivered;
      int64_t idle_ms = absl::ToInt64Milliseconds(idle_duration);

      if (idle_ms >= min_idle_time || min_idle_time == 0) {
        // Transfer to new consumer
        entry_it->second.consumer = consumer;
        entry_it->second.delivery_count++;
        entry_it->second.last_delivered = now;

        if (just_id) {
          protocol::RespValue id_val;
          id_val.SetString(id.ToString(), protocol::RespType::kBulkString);
          result.push_back(id_val);
        } else {
          // Return full entry
          protocol::RespValue entry_val =
              StreamEntryToResp(id, entry_it->second.entry.fields);
          result.push_back(entry_val);
        }
      }
    }
  }

  protocol::RespValue response;
  response.SetArray(std::move(result));
  return CommandResult(response);
}

// XAUTOCLAIM key group consumer min-idle-time start [COUNT count] [JUSTID]
// Automatically claim entries from a consumer that is idle
CommandResult HandleXAutoClaim(const protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() < 5) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'XAUTOCLAIM' command");
  }

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  const std::string& key = command[0].AsString();
  const std::string& group = command[1].AsString();
  const std::string& consumer = command[2].AsString();
  int64_t min_idle_time;

  if (!absl::SimpleAtoi(command[3].AsString(), &min_idle_time)) {
    return CommandResult(false, "ERR invalid min-idle-time");
  }

  StreamId start_id(command[4].AsString());

  // Parse options
  size_t count = 100;
  bool just_id = false;

  for (size_t i = 5; i < command.ArgCount(); ++i) {
    std::string arg = command[i].AsString();

    if (absl::AsciiStrToUpper(arg) == "COUNT") {
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR syntax error");
      }
      if (!absl::SimpleAtoi(command[++i].AsString(),
                            reinterpret_cast<int64_t*>(&count))) {
        return CommandResult(false, "ERR invalid count");
      }
    } else if (absl::AsciiStrToUpper(arg) == "JUSTID") {
      just_id = true;
    }
  }

  StreamData* stream = db->GetStream(key);
  if (!stream) {
    // Return start ID and empty array
    std::vector<protocol::RespValue> result;
    protocol::RespValue next_id;
    next_id.SetString(start_id.ToString(), protocol::RespType::kBulkString);
    result.push_back(next_id);

    protocol::RespValue empty_arr;
    result.push_back(empty_arr);

    protocol::RespValue response;
    response.SetArray(std::move(result));
    return CommandResult(response);
  }

  auto group_it = stream->groups.find(group);
  if (group_it == stream->groups.end()) {
    return CommandResult(false, "ERR no such group");
  }

  std::vector<protocol::RespValue> claimed;
  StreamId next_id = start_id;
  absl::Time now = absl::Now();

  for (auto& [id, pending] : group_it->second.pending_entries) {
    if (claimed.size() >= count) {
      next_id = id;
      break;
    }

    if (id <= start_id) {
      continue;
    }

    auto idle_duration = now - pending.last_delivered;
    int64_t idle_ms = absl::ToInt64Milliseconds(idle_duration);

    if (idle_ms >= min_idle_time) {
      pending.consumer = consumer;
      pending.delivery_count++;
      pending.last_delivered = now;

      if (just_id) {
        protocol::RespValue id_val;
        id_val.SetString(id.ToString(), protocol::RespType::kBulkString);
        claimed.push_back(id_val);
      } else {
        protocol::RespValue entry_val =
            StreamEntryToResp(id, pending.entry.fields);
        claimed.push_back(entry_val);
      }
    }
  }

  std::vector<protocol::RespValue> result;
  protocol::RespValue next_id_val;
  next_id_val.SetString(next_id.ToString(), protocol::RespType::kBulkString);
  result.push_back(next_id_val);

  protocol::RespValue claimed_arr;
  claimed_arr.SetArray(std::move(claimed));
  result.push_back(claimed_arr);

  protocol::RespValue response;
  response.SetArray(std::move(result));
  return CommandResult(response);
}

// XPENDING key group [start end count] [consumer]
// Return information about pending entries in a stream group
CommandResult HandleXPending(const protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'XPENDING' command");
  }

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  const std::string& key = command[0].AsString();
  const std::string& group = command[1].AsString();

  StreamData* stream = db->GetStream(key);
  if (!stream) {
    return CommandResult(false, "ERR no such key");
  }

  auto group_it = stream->groups.find(group);
  if (group_it == stream->groups.end()) {
    return CommandResult(false, "ERR no such group");
  }

  // Simple case: just return count
  if (command.ArgCount() == 2) {
    protocol::RespValue response;
    response.SetInteger(
        static_cast<int64_t>(group_it->second.pending_entries.size()));
    return CommandResult(response);
  }

  // Detailed case: return range of pending entries
  StreamId start_id("-");
  StreamId end_id("+");
  size_t count = 10;
  std::string consumer_filter;

  if (command.ArgCount() >= 3) {
    start_id = StreamId(command[2].AsString());
  }
  if (command.ArgCount() >= 4) {
    end_id = StreamId(command[3].AsString());
  }
  if (command.ArgCount() >= 5) {
    if (!absl::SimpleAtoi(command[4].AsString(),
                          reinterpret_cast<int64_t*>(&count))) {
      return CommandResult(false, "ERR invalid count");
    }
  }
  if (command.ArgCount() >= 6) {
    consumer_filter = command[5].AsString();
  }

  std::vector<protocol::RespValue> result;
  size_t processed = 0;

  for (const auto& [id, pending] : group_it->second.pending_entries) {
    if (processed >= count) break;

    if (id < start_id || id > end_id) {
      continue;
    }

    if (!consumer_filter.empty() && pending.consumer != consumer_filter) {
      continue;
    }

    // Return [id, consumer, idle_time, delivery_count]
    std::vector<protocol::RespValue> entry_info;

    protocol::RespValue id_val;
    id_val.SetString(id.ToString(), protocol::RespType::kBulkString);
    entry_info.push_back(id_val);

    protocol::RespValue consumer_val;
    consumer_val.SetString(pending.consumer, protocol::RespType::kBulkString);
    entry_info.push_back(consumer_val);

    auto idle_duration = absl::Now() - pending.last_delivered;
    protocol::RespValue idle_val;
    idle_val.SetInteger(absl::ToInt64Milliseconds(idle_duration));
    entry_info.push_back(idle_val);

    protocol::RespValue delivery_val;
    delivery_val.SetInteger(pending.delivery_count);
    entry_info.push_back(delivery_val);

    protocol::RespValue entry_arr;
    entry_arr.SetArray(std::move(entry_info));
    result.push_back(entry_arr);

    processed++;
  }

  protocol::RespValue response;
  response.SetArray(std::move(result));
  return CommandResult(response);
}

// XREVRANGE key end start [COUNT count]
// Return a range of entries in reverse order
CommandResult HandleXRevRange(const protocol::Command& command,
                              CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'XREVRANGE' command");
  }

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  const std::string& key = command[0].AsString();
  StreamId end_id(command[1].AsString());
  StreamId start_id(command[2].AsString());

  size_t count = SIZE_MAX;
  for (size_t i = 3; i < command.ArgCount(); ++i) {
    std::string arg = command[i].AsString();
    if (absl::AsciiStrToUpper(arg) == "COUNT") {
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR syntax error");
      }
      if (!absl::SimpleAtoi(command[++i].AsString(),
                            reinterpret_cast<int64_t*>(&count))) {
        return CommandResult(false, "ERR invalid count");
      }
    }
  }

  StreamData* stream = db->GetStream(key);
  if (!stream) {
    protocol::RespValue response;
    response.SetArray(std::vector<protocol::RespValue>{});
    return CommandResult(response);
  }

  std::vector<protocol::RespValue> result;
  size_t processed = 0;

  // Iterate in reverse order
  for (auto it = stream->entries.rbegin(); it != stream->entries.rend(); ++it) {
    if (processed >= count) break;

    if (it->id < start_id || it->id > end_id) {
      continue;
    }

    result.push_back(StreamEntryToResp(it->id, it->fields));
    processed++;
  }

  protocol::RespValue response;
  response.SetArray(std::move(result));
  return CommandResult(response);
}

// XSETID key last-id
// Set the last generated ID for a stream
CommandResult HandleXSetId(const protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'XSETID' command");
  }

  auto* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR no database available");
  }

  const std::string& key = command[0].AsString();
  StreamId last_id(command[1].AsString());

  StreamData* stream = db->GetStream(key);
  if (!stream) {
    return CommandResult(false, "ERR no such key");
  }

  // Set the last generated ID
  stream->last_id = last_id;

  protocol::RespValue ok_val;
  ok_val.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(ok_val);
}

// Register Stream commands
ASTRADB_REGISTER_COMMAND(XADD, -5, "write", RoutingStrategy::kByFirstKey,
                         HandleXAdd);
ASTRADB_REGISTER_COMMAND(XREAD, -4, "readonly", RoutingStrategy::kNone,
                         HandleXRead);
ASTRADB_REGISTER_COMMAND(XRANGE, -4, "readonly", RoutingStrategy::kByFirstKey,
                         HandleXRange);
ASTRADB_REGISTER_COMMAND(XLEN, 2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleXLen);
ASTRADB_REGISTER_COMMAND(XDEL, -3, "write", RoutingStrategy::kByFirstKey,
                         HandleXDel);
ASTRADB_REGISTER_COMMAND(XTRIM, -4, "write", RoutingStrategy::kByFirstKey,
                         HandleXTrim);
ASTRADB_REGISTER_COMMAND(XGROUP, -4, "write", RoutingStrategy::kByFirstKey,
                         HandleXGroup);
ASTRADB_REGISTER_COMMAND(XREADGROUP, -7, "write", RoutingStrategy::kNone,
                         HandleXReadGroup);
ASTRADB_REGISTER_COMMAND(XACK, -4, "write", RoutingStrategy::kByFirstKey,
                         HandleXAck);
ASTRADB_REGISTER_COMMAND(XINFO, -2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleXInfo);
ASTRADB_REGISTER_COMMAND(XCLAIM, -5, "write", RoutingStrategy::kByFirstKey,
                         HandleXClaim);
ASTRADB_REGISTER_COMMAND(XAUTOCLAIM, -5, "write", RoutingStrategy::kByFirstKey,
                         HandleXAutoClaim);
ASTRADB_REGISTER_COMMAND(XPENDING, -2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleXPending);
ASTRADB_REGISTER_COMMAND(XREVRANGE, -4, "readonly",
                         RoutingStrategy::kByFirstKey, HandleXRevRange);
ASTRADB_REGISTER_COMMAND(XSETID, 3, "write", RoutingStrategy::kByFirstKey,
                         HandleXSetId);

}  // namespace astra::commands
