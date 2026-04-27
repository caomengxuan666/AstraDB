// ==============================================================================
// Vector Search Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "vector_commands.hpp"

#include <absl/strings/numbers.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

#include "astra/base/logging.hpp"
#include "astra/container/vector_index_manager.hpp"
#include "astra/container/vector_types.hpp"
#include "astra/protocol/resp/resp_builder.hpp"
#include "astra/server/worker_scheduler.hpp"
#include "../command_auto_register.hpp"
#include "../database.hpp"

namespace astra::commands {

using astra::container::DistanceMetric;
using astra::container::IndexConfig;
using astra::protocol::RespType;
using astra::protocol::RespValue;

namespace {

DistanceMetric ParseDistanceMetric(const std::string& s) {
  std::string lower = s;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  if (lower == "cosine") return DistanceMetric::kCosine;
  if (lower == "l2") return DistanceMetric::kL2;
  if (lower == "ip") return DistanceMetric::kInnerProduct;
  return DistanceMetric::kCosine;
}

bool ParseFloatVecFromArg(const protocol::CommandArg& arg,
                           std::vector<float>& out) {
  if (!arg.IsBulkString()) return false;
  const std::string& raw = arg.AsString();
  if (raw.size() % sizeof(float) != 0) return false;
  size_t count = raw.size() / sizeof(float);
  out.resize(count);
  std::memcpy(out.data(), raw.data(), raw.size());
  return true;
}

RespValue MakeOkResponse() {
  RespValue resp;
  resp.SetString(std::string("OK"), RespType::kSimpleString);
  return resp;
}

}  // namespace

CommandResult HandleVCreate(const protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'VCREATE'");
  }

  std::string name = command[0].AsString();

  int dim_val = 0;
  if (!absl::SimpleAtoi(command[1].AsString(), &dim_val) || dim_val == 0) {
    return CommandResult(false, "ERR invalid DIM value");
  }
  uint32_t dim = static_cast<uint32_t>(dim_val);

  DistanceMetric dist = ParseDistanceMetric(command[2].AsString());
  uint32_t M = 16;
  uint32_t ef = 200;

  for (size_t i = 3; i < command.ArgCount(); i++) {
    std::string opt = command[i].AsString();
    std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
    if (opt == "M" && i + 1 < command.ArgCount()) {
      int val = 0;
      if (absl::SimpleAtoi(command[++i].AsString(), &val))
        M = static_cast<uint32_t>(val);
    } else if (opt == "EF" && i + 1 < command.ArgCount()) {
      int val = 0;
      if (absl::SimpleAtoi(command[++i].AsString(), &val))
        ef = static_cast<uint32_t>(val);
    }
  }

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  // Create index on current worker first
  db->VectorCreateIndex(name, dim, dist, M, ef);

  // NO SHARING: broadcast to OTHER workers via AddTask
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler) {
    auto* current_worker = context->GetWorker();
    auto all_workers = worker_scheduler->GetAllWorkers();
    for (auto* target : all_workers) {
      if (target == current_worker) continue;
      target->AddTask([target, name, dim, dist, M, ef]() {
        target->GetDataShard().GetDatabase().VectorCreateIndex(
            name, dim, dist, M, ef);
      });
    }
  }
  return CommandResult(MakeOkResponse());
}

CommandResult HandleVDrop(const protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'VDROP'");
  }

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  std::string name = command[0].AsString();

  // Drop on current worker first
  db->VectorDropIndex(name);

  // Broadcast to other workers
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler) {
    auto* current_worker = context->GetWorker();
    auto all_workers = worker_scheduler->GetAllWorkers();
    for (auto* target : all_workers) {
      if (target == current_worker) continue;
      target->AddTask([target, name]() {
        target->GetDataShard().GetDatabase().VectorDropIndex(name);
      });
    }
  }
  return CommandResult(MakeOkResponse());
}

CommandResult HandleVList(const protocol::Command& /*command*/,
                           CommandContext* context) {
  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  auto names = db->VectorListIndexes();

  std::vector<RespValue> result;
  result.reserve(names.size());
  for (const auto& name : names) {
    auto stats = db->VectorGetStats(name);

    std::vector<RespValue> entry;
    entry.reserve(6);
    entry.push_back(RespValue(std::string(name)));
    entry.push_back(RespValue(static_cast<int64_t>(stats.dimension)));
    entry.push_back(RespValue(
        std::string(astra::container::DistanceMetricToString(stats.distance_metric))));
    entry.push_back(RespValue(static_cast<int64_t>(stats.M)));
    entry.push_back(RespValue(static_cast<int64_t>(stats.current_ef)));
    entry.push_back(RespValue(static_cast<int64_t>(stats.vector_count)));
    result.push_back(RespValue(std::move(entry)));
  }

  return CommandResult(RespValue(std::move(result)));
}

CommandResult HandleVSet(const protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'VSET'");
  }

  std::string index_name = command[0].AsString();
  std::string key = command[1].AsString();
  std::vector<float> vec;
  if (!ParseFloatVecFromArg(command[2], vec)) {
    return CommandResult(false, "ERR invalid vector data");
  }

  std::unordered_map<std::string, container::MetaValue> metadata;
  for (size_t i = 3; i + 1 < command.ArgCount(); i += 2) {
    std::string meta_key = command[i].AsString();
    std::string meta_val = command[i + 1].AsString();
    metadata[meta_key] = meta_val;
  }

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  if (!db->VectorHasIndex(index_name)) {
    return CommandResult(false, "ERR index not found: " + index_name);
  }

  bool ok = db->VecSet(index_name, key, vec, metadata);
  if (!ok) {
    return CommandResult(false, "ERR failed to set vector");
  }
  return CommandResult(MakeOkResponse());
}

CommandResult HandleMVSet(const protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'MVSET'");
  }

  std::string index_name = command[0].AsString();
  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  if (!db->VectorHasIndex(index_name)) {
    return CommandResult(false, "ERR index not found: " + index_name);
  }

  size_t count = 0;
  size_t i = 1;
  while (i < command.ArgCount()) {
    std::string key = command[i].AsString();
    if (i + 1 >= command.ArgCount()) break;
    i++;
    std::vector<float> vec;
    if (!ParseFloatVecFromArg(command[i], vec)) break;
    i++;

    std::unordered_map<std::string, container::MetaValue> metadata;
    while (i + 1 < command.ArgCount()) {
      std::string meta_key = command[i].AsString();
      i++;
      if (i >= command.ArgCount()) break;
      std::string meta_val = command[i].AsString();
      metadata[meta_key] = meta_val;
      i++;
    }

    if (db->VecSet(index_name, key, vec, metadata)) count++;
  }

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

CommandResult HandleVGet(const protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'VGET'");
  }

  std::string key = command[0].AsString();
  bool with_vector = false;
  bool with_meta = false;

  for (size_t i = 1; i < command.ArgCount(); i++) {
    std::string opt = command[i].AsString();
    std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
    if (opt == "VECTOR") with_vector = true;
    else if (opt == "META") with_meta = true;
  }

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  auto entry = db->VecGet(key);
  if (!entry.has_value()) {
    return CommandResult(RespValue(RespType::kNullBulkString));
  }

  std::vector<RespValue> result;
  result.push_back(RespValue(std::string(key)));

  if (with_vector) {
    std::string raw(reinterpret_cast<const char*>(entry->vector.data()),
                     entry->vector.size() * sizeof(float));
    result.push_back(RespValue(std::move(raw)));
  }

  if (with_meta) {
    std::vector<RespValue> meta_arr;
    for (const auto& [mk, mv] : entry->metadata) {
      meta_arr.push_back(RespValue(std::string(mk)));
      if (auto* s = std::get_if<std::string>(&mv)) {
        meta_arr.push_back(RespValue(std::string(*s)));
      } else if (auto* i = std::get_if<int64_t>(&mv)) {
        meta_arr.push_back(RespValue(*i));
      } else if (auto* d = std::get_if<double>(&mv)) {
        meta_arr.push_back(RespValue(*d));
      }
    }
    result.push_back(RespValue(std::move(meta_arr)));
  }

  return CommandResult(RespValue(std::move(result)));
}

CommandResult HandleVDel(const protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'VDEL'");
  }

  std::string index_name = command[0].AsString();
  std::string key = command[1].AsString();

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  bool ok = db->VecDel(index_name, key);
  return CommandResult(RespValue(static_cast<int64_t>(ok ? 1 : 0)));
}

CommandResult HandleVSearch(const protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'VSEARCH'");
  }

  std::string index_name = command[0].AsString();
  std::vector<float> query_vec;
  if (!ParseFloatVecFromArg(command[1], query_vec)) {
    return CommandResult(false, "ERR invalid query vector");
  }

  int k_val = 10;
  if (!absl::SimpleAtoi(command[2].AsString(), &k_val)) {
    return CommandResult(false, "ERR invalid K value");
  }
  size_t k = static_cast<size_t>(k_val);

  bool with_score = false;
  for (size_t i = 3; i < command.ArgCount(); i++) {
    std::string opt = command[i].AsString();
    std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
    if (opt == "WITHSCORE") with_score = true;
  }

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  if (!db->VectorHasIndex(index_name)) {
    return CommandResult(false, "ERR index not found: " + index_name);
  }

  auto results = db->VecSearch(index_name, query_vec, k);

  std::vector<RespValue> result;
  result.reserve(results.size());
  for (const auto& [id, score] : results) {
    std::vector<RespValue> entry;
    entry.reserve(with_score ? 2 : 1);
    entry.push_back(RespValue(static_cast<int64_t>(id)));
    if (with_score) {
      entry.push_back(RespValue(static_cast<double>(score)));
    }
    result.push_back(RespValue(std::move(entry)));
  }

  return CommandResult(RespValue(std::move(result)));
}

CommandResult HandleVInfo(const protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'VINFO'");
  }

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  std::string index_name = command[0].AsString();
  auto stats = db->VectorGetStats(index_name);

  std::vector<RespValue> result;
  result.reserve(14);
  result.push_back(RespValue(std::string("index_name")));
  result.push_back(RespValue(std::string(index_name)));
  result.push_back(RespValue(std::string("dimension")));
  result.push_back(RespValue(static_cast<int64_t>(stats.dimension)));
  result.push_back(RespValue(std::string("distance_metric")));
  result.push_back(RespValue(
      std::string(astra::container::DistanceMetricToString(stats.distance_metric))));
  result.push_back(RespValue(std::string("M")));
  result.push_back(RespValue(static_cast<int64_t>(stats.M)));
  result.push_back(RespValue(std::string("ef_construction")));
  result.push_back(RespValue(static_cast<int64_t>(stats.ef_construction)));
  result.push_back(RespValue(std::string("current_ef")));
  result.push_back(RespValue(static_cast<int64_t>(stats.current_ef)));
  result.push_back(RespValue(std::string("vector_count")));
  result.push_back(RespValue(static_cast<int64_t>(stats.vector_count)));

  return CommandResult(RespValue(std::move(result)));
}

CommandResult HandleVCompact(const protocol::Command& command,
                              CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'VCOMPACT'");
  }

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  std::string index_name = command[0].AsString();
  db->CompactVectorIndex(index_name);
  return CommandResult(MakeOkResponse());
}

ASTRADB_REGISTER_COMMAND(VCREATE, -3, "write", RoutingStrategy::kNone,
                         HandleVCreate);
ASTRADB_REGISTER_COMMAND(VDROP, 2, "write", RoutingStrategy::kNone,
                         HandleVDrop);
ASTRADB_REGISTER_COMMAND(VLIST, -1, "readonly", RoutingStrategy::kNone,
                         HandleVList);
ASTRADB_REGISTER_COMMAND(VSET, -4, "write", RoutingStrategy::kByFirstKey,
                         HandleVSet);
ASTRADB_REGISTER_COMMAND(MVSET, -3, "write", RoutingStrategy::kNone,
                         HandleMVSet);
ASTRADB_REGISTER_COMMAND(VGET, -2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleVGet);
ASTRADB_REGISTER_COMMAND(VDEL, 3, "write", RoutingStrategy::kByFirstKey,
                         HandleVDel);
ASTRADB_REGISTER_COMMAND(VSEARCH, -4, "readonly", RoutingStrategy::kNone,
                         HandleVSearch);
ASTRADB_REGISTER_COMMAND(VINFO, 2, "readonly", RoutingStrategy::kNone,
                         HandleVInfo);
ASTRADB_REGISTER_COMMAND(VCOMPACT, 2, "write", RoutingStrategy::kNone,
                         HandleVCompact);

}  // namespace astra::commands
