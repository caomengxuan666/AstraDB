// ==============================================================================
// Vector Search Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "vector_commands.hpp"

#include <absl/strings/ascii.h>
#include <absl/strings/numbers.h>
#include <absl/time/clock.h>
#include <absl/time/time.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <sstream>
#include <thread>

#include "astra/commands/astra/vector_search_runtime.hpp"
#include "astra/base/logging.hpp"
#include "astra/cluster/shard_manager.hpp"
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
using astra::server::WorkerCommandContext;
using SearchHits = std::vector<std::pair<uint64_t, float>>;

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

size_t RouteKeyToWorker(absl::string_view key, size_t worker_count) {
  if (worker_count == 0) {
    return 0;
  }
  const auto slot =
      static_cast<size_t>(cluster::HashSlotCalculator::CalculateWithTag(key));
  if ((worker_count & (worker_count - 1)) == 0) {
    return slot & (worker_count - 1);
  }
  return slot % worker_count;
}

std::string ToUpperCopy(const std::string& s) {
  std::string out = s;
  absl::AsciiStrToUpper(&out);
  return out;
}

void WaitFutureWithWorkerPump(std::future<void>& future,
                              CommandContext* context) {
  auto* current_worker = context ? context->GetWorker() : nullptr;
  server::WorkerScheduler::WaitFutureWithCallerPump(future, current_worker);
}

template <typename T>
T WaitFutureWithWorkerPump(std::future<T>& future, CommandContext* context) {
  auto* current_worker = context ? context->GetWorker() : nullptr;
  return server::WorkerScheduler::WaitFutureWithCallerPump(future,
                                                            current_worker);
}

VectorSearchMode GetVectorSearchModeFromContext(CommandContext* context) {
  if (auto* worker_ctx = dynamic_cast<WorkerCommandContext*>(context)) {
    return worker_ctx->GetVectorSearchMode();
  }
  return VectorSearchMode::kGlobal;
}

const char* VectorSearchModeToString(VectorSearchMode mode) {
  return mode == VectorSearchMode::kLocal ? "LOCAL" : "GLOBAL";
}

RespValue BuildSearchResp(const SearchHits& results, bool with_score) {
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
  return RespValue(std::move(result));
}

SearchHits ExecuteLocalSearch(const std::string& index_name,
                             const std::vector<float>& query_vec, size_t k,
                             CommandContext* context) {
  Database* db = context->GetDatabase();
  if (!db) {
    return {};
  }

  auto* worker_scheduler = context->GetWorkerScheduler();
  auto* current_worker = context->GetWorker();
  if (!worker_scheduler || !current_worker || worker_scheduler->size() <= 1) {
    return db->VecSearch(index_name, query_vec, k);
  }

  auto all_workers = worker_scheduler->GetAllWorkers();
  const size_t target_worker_id =
      RouteKeyToWorker(index_name, all_workers.size());
  auto* target_worker = all_workers[target_worker_id];

  if (target_worker == current_worker) {
    return db->VecSearch(index_name, query_vec, k);
  }

  auto shared_query = std::make_shared<const std::vector<float>>(query_vec);
  auto promise = std::make_shared<std::promise<SearchHits>>();
  auto future = promise->get_future();
  target_worker->AddTask(
      [target_worker, promise, index_name, shared_query, k]() mutable {
        try {
          auto results = target_worker->GetDataShard().GetDatabase().VecSearch(
              index_name, *shared_query, k);
          promise->set_value(std::move(results));
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });
  target_worker->NotifyTaskProcessing();
  return WaitFutureWithWorkerPump(future, context);
}

SearchHits ExecuteGlobalSearch(const std::string& index_name,
                              const std::vector<float>& query_vec, size_t k,
                              CommandContext* context) {
  Database* db = context->GetDatabase();
  if (!db) {
    return {};
  }

  SearchHits results;
  auto* worker_scheduler = context->GetWorkerScheduler();
  auto* current_worker = context->GetWorker();
  if (!worker_scheduler || !current_worker || worker_scheduler->size() <= 1) {
    return db->VecSearch(index_name, query_vec, k);
  }

  auto all_workers = worker_scheduler->GetAllWorkers();
  std::vector<std::future<SearchHits>> futures;
  futures.reserve(all_workers.size());
  auto shared_query = std::make_shared<const std::vector<float>>(query_vec);

  for (auto* target_worker : all_workers) {
    if (target_worker == current_worker) {
      auto local_results = db->VecSearch(index_name, query_vec, k);
      results.insert(results.end(), local_results.begin(), local_results.end());
      continue;
    }

    auto promise = std::make_shared<std::promise<SearchHits>>();
    futures.push_back(promise->get_future());
    target_worker->AddTask(
        [target_worker, promise, index_name, shared_query, k]() mutable {
          try {
            auto task_results = target_worker->GetDataShard().GetDatabase().VecSearch(
                index_name, *shared_query, k);
            promise->set_value(std::move(task_results));
          } catch (...) {
            promise->set_exception(std::current_exception());
          }
        });
    target_worker->NotifyTaskProcessing();
  }

  for (auto& future : futures) {
    auto worker_results = WaitFutureWithWorkerPump(future, context);
    results.insert(results.end(), worker_results.begin(), worker_results.end());
  }

  std::sort(results.begin(), results.end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.second < rhs.second;
            });
  if (results.size() > k) {
    results.resize(k);
  }
  return results;
}

SearchHits ExecuteSearchByMode(const std::string& index_name,
                              const std::vector<float>& query_vec, size_t k,
                              VectorSearchMode mode, CommandContext* context) {
  if (mode == VectorSearchMode::kLocal) {
    return ExecuteLocalSearch(index_name, query_vec, k, context);
  }
  return ExecuteGlobalSearch(index_name, query_vec, k, context);
}

std::string MakeJobId(size_t owner_worker_id, uint64_t seq) {
  return std::to_string(owner_worker_id) + ":" + std::to_string(seq);
}

bool ParseJobId(absl::string_view job_id, size_t* owner_worker_id,
                uint64_t* seq) {
  size_t pos = job_id.find(':');
  if (pos == absl::string_view::npos || pos == 0 || pos + 1 >= job_id.size()) {
    return false;
  }
  return absl::SimpleAtoi(job_id.substr(0, pos), owner_worker_id) &&
         absl::SimpleAtoi(job_id.substr(pos + 1), seq);
}

CommandResult GetAsyncJobResult(const std::shared_ptr<VectorSearchJob>& job) {
  const auto status = job->status.load(std::memory_order_acquire);
  if (status == VectorSearchJobStatus::kPending) {
    return CommandResult(RespValue(std::string("PENDING")));
  }
  if (status == VectorSearchJobStatus::kCanceled) {
    return CommandResult(RespValue(std::string("CANCELED")));
  }
  if (status == VectorSearchJobStatus::kError) {
    std::string err;
    {
      absl::MutexLock lock(&job->mutex);
      err = job->error;
    }
    return CommandResult(false, err.empty() ? "ERR vector async job failed" : err);
  }

  SearchHits results;
  bool with_score = false;
  {
    absl::MutexLock lock(&job->mutex);
    results = job->results;
    with_score = job->with_score;
  }
  return CommandResult(BuildSearchResp(results, with_score));
}

void LaunchVectorSearchAsyncJob(CommandContext* context,
                                const std::shared_ptr<VectorSearchJob>& job) {
  std::thread([context, job]() {
    try {
      if (job->status.load(std::memory_order_acquire) ==
          VectorSearchJobStatus::kCanceled) {
        job->completed.Notify();
        return;
      }
      auto results = ExecuteSearchByMode(job->index_name, job->query_vec, job->k,
                                         job->mode, context);
      if (job->status.load(std::memory_order_acquire) ==
          VectorSearchJobStatus::kCanceled) {
        job->completed.Notify();
        return;
      }
      {
        absl::MutexLock lock(&job->mutex);
        job->results = std::move(results);
      }
      job->status.store(VectorSearchJobStatus::kDone, std::memory_order_release);
    } catch (const std::exception& ex) {
      {
        absl::MutexLock lock(&job->mutex);
        job->error = std::string("ERR vector async job failed: ") + ex.what();
      }
      job->status.store(VectorSearchJobStatus::kError, std::memory_order_release);
    } catch (...) {
      {
        absl::MutexLock lock(&job->mutex);
        job->error = "ERR vector async job failed: unknown exception";
      }
      job->status.store(VectorSearchJobStatus::kError, std::memory_order_release);
    }
    job->completed.Notify();
  }).detach();
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
    std::vector<std::future<void>> futures;
    futures.reserve(all_workers.size());
    for (auto* target : all_workers) {
      if (target == current_worker) continue;
      auto promise = std::make_shared<std::promise<void>>();
      futures.push_back(promise->get_future());
      target->AddTask([target, name, dim, dist, M, ef, promise]() mutable {
        try {
          target->GetDataShard().GetDatabase().VectorCreateIndex(name, dim, dist,
                                                                 M, ef);
          promise->set_value();
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });
      target->NotifyTaskProcessing();
    }
    for (auto& future : futures) {
      WaitFutureWithWorkerPump(future, context);
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
    std::vector<std::future<void>> futures;
    futures.reserve(all_workers.size());
    for (auto* target : all_workers) {
      if (target == current_worker) continue;
      auto promise = std::make_shared<std::promise<void>>();
      futures.push_back(promise->get_future());
      target->AddTask([target, name, promise]() mutable {
        try {
          target->GetDataShard().GetDatabase().VectorDropIndex(name);
          promise->set_value();
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });
      target->NotifyTaskProcessing();
    }
    for (auto& future : futures) {
      WaitFutureWithWorkerPump(future, context);
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

  bool ok = false;
  auto* worker_scheduler = context->GetWorkerScheduler();
  auto* current_worker = context->GetWorker();
  if (worker_scheduler && current_worker && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    const size_t target_worker_id = RouteKeyToWorker(key, all_workers.size());
    auto* target_worker = all_workers[target_worker_id];
    if (target_worker == current_worker) {
      ok = db->VecSet(index_name, key, vec, metadata);
    } else {
      auto promise = std::make_shared<std::promise<bool>>();
      auto future = promise->get_future();
      target_worker->AddTask([target_worker, promise, index_name, key, vec,
                              metadata]() mutable {
        try {
          bool task_ok =
              target_worker->GetDataShard().GetDatabase().VecSet(index_name, key,
                                                                 vec, metadata);
          promise->set_value(task_ok);
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });
      target_worker->NotifyTaskProcessing();
      ok = WaitFutureWithWorkerPump(future, context);
    }
  } else {
    ok = db->VecSet(index_name, key, vec, metadata);
  }
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

  std::optional<container::VectorEntry> entry;
  auto* worker_scheduler = context->GetWorkerScheduler();
  auto* current_worker = context->GetWorker();
  if (worker_scheduler && current_worker && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    const size_t target_worker_id = RouteKeyToWorker(key, all_workers.size());
    auto* target_worker = all_workers[target_worker_id];
    if (target_worker == current_worker) {
      entry = db->VecGet(key);
    } else {
      auto promise =
          std::make_shared<std::promise<std::optional<container::VectorEntry>>>();
      auto future = promise->get_future();
      target_worker->AddTask([target_worker, promise, key]() mutable {
        try {
          auto task_entry = target_worker->GetDataShard().GetDatabase().VecGet(key);
          promise->set_value(task_entry);
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });
      target_worker->NotifyTaskProcessing();
      entry = WaitFutureWithWorkerPump(future, context);
    }
  } else {
    entry = db->VecGet(key);
  }

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

  bool ok = false;
  auto* worker_scheduler = context->GetWorkerScheduler();
  auto* current_worker = context->GetWorker();
  if (worker_scheduler && current_worker && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    const size_t target_worker_id = RouteKeyToWorker(key, all_workers.size());
    auto* target_worker = all_workers[target_worker_id];
    if (target_worker == current_worker) {
      ok = db->VecDel(index_name, key);
    } else {
      auto promise = std::make_shared<std::promise<bool>>();
      auto future = promise->get_future();
      target_worker->AddTask([target_worker, promise, index_name,
                              key]() mutable {
        try {
          bool task_ok =
              target_worker->GetDataShard().GetDatabase().VecDel(index_name, key);
          promise->set_value(task_ok);
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });
      target_worker->NotifyTaskProcessing();
      ok = WaitFutureWithWorkerPump(future, context);
    }
  } else {
    ok = db->VecDel(index_name, key);
  }
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
    std::string opt = ToUpperCopy(command[i].AsString());
    if (opt == "WITHSCORE") with_score = true;
  }

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");

  if (!db->VectorHasIndex(index_name)) {
    return CommandResult(false, "ERR index not found: " + index_name);
  }

  const VectorSearchMode mode = GetVectorSearchModeFromContext(context);
  auto results = ExecuteSearchByMode(index_name, query_vec, k, mode, context);
  return CommandResult(BuildSearchResp(results, with_score));
}

CommandResult HandleVSearchAsync(const protocol::Command& command,
                                 CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'VSEARCHASYNC'");
  }

  auto* worker_ctx = dynamic_cast<WorkerCommandContext*>(context);
  auto* current_worker = context->GetWorker();
  if (!worker_ctx || !current_worker) {
    return CommandResult(false, "ERR VSEARCHASYNC requires worker context");
  }

  std::string index_name = command[0].AsString();
  std::vector<float> query_vec;
  if (!ParseFloatVecFromArg(command[1], query_vec)) {
    return CommandResult(false, "ERR invalid query vector");
  }

  int k_val = 10;
  if (!absl::SimpleAtoi(command[2].AsString(), &k_val) || k_val <= 0) {
    return CommandResult(false, "ERR invalid K value");
  }
  const size_t k = static_cast<size_t>(k_val);

  Database* db = context->GetDatabase();
  if (!db) return CommandResult(false, "ERR database not initialized");
  if (!db->VectorHasIndex(index_name)) {
    return CommandResult(false, "ERR index not found: " + index_name);
  }

  bool with_score = false;
  for (size_t i = 3; i < command.ArgCount(); ++i) {
    std::string opt = ToUpperCopy(command[i].AsString());
    if (opt == "WITHSCORE") {
      with_score = true;
      continue;
    }
    return CommandResult(false, "ERR syntax error");
  }

  const VectorSearchMode mode = worker_ctx->GetVectorSearchMode();
  auto& table = worker_ctx->GetVectorSearchJobTable();
  auto job = table.CreateJob(with_score, index_name, std::move(query_vec), k, mode);
  const std::string job_id = MakeJobId(current_worker->GetWorkerId(), job->job_seq);

  LaunchVectorSearchAsyncJob(context, job);
  return CommandResult(RespValue(job_id));
}

CommandResult HandleVSearchGet(const protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'VSEARCHGET'");
  }

  size_t owner_worker_id = 0;
  uint64_t seq = 0;
  const std::string job_id = command[0].AsString();
  if (!ParseJobId(job_id, &owner_worker_id, &seq)) {
    return CommandResult(false, "ERR invalid job_id");
  }

  auto* worker_scheduler = context->GetWorkerScheduler();
  auto* current_worker = context->GetWorker();
  if (!worker_scheduler || !current_worker) {
    return CommandResult(false, "ERR VSEARCHGET requires worker context");
  }
  if (owner_worker_id >= worker_scheduler->size()) {
    return CommandResult(false, "ERR invalid job_id");
  }

  if (owner_worker_id != current_worker->GetWorkerId()) {
    auto* owner_worker = worker_scheduler->GetWorker(owner_worker_id);
    auto promise = std::make_shared<std::promise<CommandResult>>();
    auto future = promise->get_future();
    owner_worker->AddTask([owner_worker, promise, seq]() mutable {
      try {
        auto* owner_ctx = owner_worker->GetDataShard().GetCommandContext();
        auto job = owner_ctx->GetVectorSearchJobTable().GetJob(seq);
        if (!job) {
          promise->set_value(CommandResult(false, "ERR job not found"));
          return;
        }
        promise->set_value(GetAsyncJobResult(job));
      } catch (const std::exception& ex) {
        promise->set_value(
            CommandResult(false, std::string("ERR VSEARCHGET failed: ") + ex.what()));
      } catch (...) {
        promise->set_value(CommandResult(false, "ERR VSEARCHGET failed"));
      }
    });
    owner_worker->NotifyTaskProcessing();
    return WaitFutureWithWorkerPump(future, context);
  }

  auto* worker_ctx = dynamic_cast<WorkerCommandContext*>(context);
  if (!worker_ctx) {
    return CommandResult(false, "ERR VSEARCHGET requires worker context");
  }
  auto job = worker_ctx->GetVectorSearchJobTable().GetJob(seq);
  if (!job) {
    return CommandResult(false, "ERR job not found");
  }
  return GetAsyncJobResult(job);
}

CommandResult HandleVSearchWait(const protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'VSEARCHWAIT'");
  }

  size_t owner_worker_id = 0;
  uint64_t seq = 0;
  const std::string job_id = command[0].AsString();
  if (!ParseJobId(job_id, &owner_worker_id, &seq)) {
    return CommandResult(false, "ERR invalid job_id");
  }

  int timeout_ms = 0;
  if (!absl::SimpleAtoi(command[1].AsString(), &timeout_ms) || timeout_ms < 0) {
    return CommandResult(false, "ERR invalid timeout");
  }

  auto* worker_scheduler = context->GetWorkerScheduler();
  auto* current_worker = context->GetWorker();
  if (!worker_scheduler || !current_worker) {
    return CommandResult(false, "ERR VSEARCHWAIT requires worker context");
  }
  if (owner_worker_id >= worker_scheduler->size()) {
    return CommandResult(false, "ERR invalid job_id");
  }

  auto wait_logic = [seq, timeout_ms](WorkerCommandContext* owner_ctx) -> CommandResult {
    auto job = owner_ctx->GetVectorSearchJobTable().GetJob(seq);
    if (!job) {
      return CommandResult(false, "ERR job not found");
    }

    const auto status = job->status.load(std::memory_order_acquire);
    if (status == VectorSearchJobStatus::kPending && timeout_ms > 0) {
      job->completed.WaitForNotificationWithTimeout(absl::Milliseconds(timeout_ms));
    }
    return GetAsyncJobResult(job);
  };

  if (owner_worker_id != current_worker->GetWorkerId()) {
    auto* owner_worker = worker_scheduler->GetWorker(owner_worker_id);
    auto promise = std::make_shared<std::promise<CommandResult>>();
    auto future = promise->get_future();
    owner_worker->AddTask([owner_worker, promise, wait_logic]() mutable {
      try {
        auto* owner_ctx = owner_worker->GetDataShard().GetCommandContext();
        promise->set_value(wait_logic(owner_ctx));
      } catch (const std::exception& ex) {
        promise->set_value(
            CommandResult(false, std::string("ERR VSEARCHWAIT failed: ") + ex.what()));
      } catch (...) {
        promise->set_value(CommandResult(false, "ERR VSEARCHWAIT failed"));
      }
    });
    owner_worker->NotifyTaskProcessing();
    return WaitFutureWithWorkerPump(future, context);
  }

  auto* worker_ctx = dynamic_cast<WorkerCommandContext*>(context);
  if (!worker_ctx) {
    return CommandResult(false, "ERR VSEARCHWAIT requires worker context");
  }
  return wait_logic(worker_ctx);
}

CommandResult HandleVSearchCancel(const protocol::Command& command,
                                  CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'VSEARCHCANCEL'");
  }

  size_t owner_worker_id = 0;
  uint64_t seq = 0;
  const std::string job_id = command[0].AsString();
  if (!ParseJobId(job_id, &owner_worker_id, &seq)) {
    return CommandResult(false, "ERR invalid job_id");
  }

  auto* worker_scheduler = context->GetWorkerScheduler();
  auto* current_worker = context->GetWorker();
  if (!worker_scheduler || !current_worker) {
    return CommandResult(false, "ERR VSEARCHCANCEL requires worker context");
  }
  if (owner_worker_id >= worker_scheduler->size()) {
    return CommandResult(false, "ERR invalid job_id");
  }

  auto cancel_logic = [seq](WorkerCommandContext* owner_ctx) -> CommandResult {
    auto job = owner_ctx->GetVectorSearchJobTable().GetJob(seq);
    if (!job) {
      return CommandResult(RespValue(static_cast<int64_t>(0)));
    }
    auto previous =
        job->status.exchange(VectorSearchJobStatus::kCanceled, std::memory_order_acq_rel);
    if (previous == VectorSearchJobStatus::kDone ||
        previous == VectorSearchJobStatus::kError ||
        previous == VectorSearchJobStatus::kCanceled) {
      return CommandResult(RespValue(static_cast<int64_t>(0)));
    }
    job->completed.Notify();
    return CommandResult(RespValue(static_cast<int64_t>(1)));
  };

  if (owner_worker_id != current_worker->GetWorkerId()) {
    auto* owner_worker = worker_scheduler->GetWorker(owner_worker_id);
    auto promise = std::make_shared<std::promise<CommandResult>>();
    auto future = promise->get_future();
    owner_worker->AddTask([owner_worker, promise, cancel_logic]() mutable {
      try {
        auto* owner_ctx = owner_worker->GetDataShard().GetCommandContext();
        promise->set_value(cancel_logic(owner_ctx));
      } catch (const std::exception& ex) {
        promise->set_value(
            CommandResult(false, std::string("ERR VSEARCHCANCEL failed: ") + ex.what()));
      } catch (...) {
        promise->set_value(CommandResult(false, "ERR VSEARCHCANCEL failed"));
      }
    });
    owner_worker->NotifyTaskProcessing();
    return WaitFutureWithWorkerPump(future, context);
  }

  auto* worker_ctx = dynamic_cast<WorkerCommandContext*>(context);
  if (!worker_ctx) {
    return CommandResult(false, "ERR VSEARCHCANCEL requires worker context");
  }
  return cancel_logic(worker_ctx);
}

CommandResult HandleVSearchMode(const protocol::Command& command,
                                CommandContext* context) {
  auto* worker_ctx = dynamic_cast<WorkerCommandContext*>(context);
  if (!worker_ctx) {
    return CommandResult(false, "ERR VSEARCHMODE requires worker context");
  }

  if (command.ArgCount() == 0) {
    return CommandResult(
        RespValue(std::string(VectorSearchModeToString(worker_ctx->GetVectorSearchMode()))));
  }

  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'VSEARCHMODE'");
  }

  const std::string mode = ToUpperCopy(command[0].AsString());
  if (mode == "LOCAL") {
    worker_ctx->SetVectorSearchMode(VectorSearchMode::kLocal);
    return CommandResult(MakeOkResponse());
  }
  if (mode == "GLOBAL") {
    worker_ctx->SetVectorSearchMode(VectorSearchMode::kGlobal);
    return CommandResult(MakeOkResponse());
  }
  return CommandResult(false, "ERR VSEARCHMODE only supports LOCAL|GLOBAL");
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

  auto* worker_scheduler = context->GetWorkerScheduler();
  auto* current_worker = context->GetWorker();
  if (worker_scheduler && current_worker && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    uint64_t total_count = stats.vector_count;
    std::vector<std::future<uint64_t>> futures;
    futures.reserve(all_workers.size());

    for (auto* target_worker : all_workers) {
      if (target_worker == current_worker) {
        continue;
      }
      auto promise = std::make_shared<std::promise<uint64_t>>();
      futures.push_back(promise->get_future());
      target_worker->AddTask([target_worker, promise, index_name]() mutable {
        try {
          auto worker_stats =
              target_worker->GetDataShard().GetDatabase().VectorGetStats(index_name);
          promise->set_value(worker_stats.vector_count);
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });
      target_worker->NotifyTaskProcessing();
    }

    for (auto& future : futures) {
      total_count += WaitFutureWithWorkerPump(future, context);
    }
    stats.vector_count = total_count;
  }

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
ASTRADB_REGISTER_COMMAND(VSET, -4, "write", RoutingStrategy::kNone,
                         HandleVSet);
ASTRADB_REGISTER_COMMAND(MVSET, -3, "write", RoutingStrategy::kNone,
                         HandleMVSet);
ASTRADB_REGISTER_COMMAND(VGET, -2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleVGet);
ASTRADB_REGISTER_COMMAND(VDEL, 3, "write", RoutingStrategy::kNone,
                         HandleVDel);
ASTRADB_REGISTER_COMMAND(VSEARCH, -4, "readonly", RoutingStrategy::kNone,
                         HandleVSearch);
ASTRADB_REGISTER_COMMAND(VSEARCHASYNC, -4, "readonly", RoutingStrategy::kNone,
                         HandleVSearchAsync);
ASTRADB_REGISTER_COMMAND(VSEARCHGET, 2, "readonly", RoutingStrategy::kNone,
                         HandleVSearchGet);
ASTRADB_REGISTER_COMMAND(VSEARCHWAIT, 3, "readonly", RoutingStrategy::kNone,
                         HandleVSearchWait);
ASTRADB_REGISTER_COMMAND(VSEARCHCANCEL, 2, "readonly", RoutingStrategy::kNone,
                         HandleVSearchCancel);
ASTRADB_REGISTER_COMMAND(VSEARCHMODE, -1, "fast", RoutingStrategy::kNone,
                         HandleVSearchMode);
ASTRADB_REGISTER_COMMAND(VINFO, 2, "readonly", RoutingStrategy::kNone,
                         HandleVInfo);
ASTRADB_REGISTER_COMMAND(VCOMPACT, 2, "write", RoutingStrategy::kNone,
                         HandleVCompact);

}  // namespace astra::commands
