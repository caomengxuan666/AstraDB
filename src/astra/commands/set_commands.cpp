// ==============================================================================
// Set Commands Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "set_commands.hpp"

#include "astra/protocol/resp/resp_builder.hpp"
#include "command_auto_register.hpp"
#include "pubsub_commands.hpp"
#include "astra/server/worker_scheduler.hpp"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace astra::commands {

// SADD key member [member ...]
CommandResult HandleSAdd(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SADD' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();
  size_t count = 0;

  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of member argument");
    }
    if (db->SAdd(key, arg.AsString())) {
      ++count;
    }
  }

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// SREM key member [member ...]
CommandResult HandleSRem(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SREM' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();
  size_t count = 0;

  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of member argument");
    }
    if (db->SRem(key, arg.AsString())) {
      ++count;
    }
  }

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// SMEMBERS key
CommandResult HandleSMembers(const astra::protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'SMEMBERS' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();
  auto members = db->SMembers(key);

  absl::InlinedVector<RespValue, 16> array;
  array.reserve(members.size());
  for (const auto& member : members) {
    array.emplace_back(RespValue(std::string(member)));
  }

  return CommandResult(
      RespValue(std::vector<RespValue>(array.begin(), array.end())));
}

// SISMEMBER key member
CommandResult HandleSIsMember(const astra::protocol::Command& command,
                              CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'SISMEMBER' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& member_arg = command[1];

  if (!key_arg.IsBulkString() || !member_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string member = member_arg.AsString();

  bool is_member = db->SIsMember(key, member);
  return CommandResult(RespValue(static_cast<int64_t>(is_member ? 1 : 0)));
}

// SCARD key
CommandResult HandleSCard(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SCARD' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();
  size_t count = db->SCard(key);
  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// SPOP key [count]
CommandResult HandleSPop(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SPOP' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();

  // For now, only support popping one element
  auto member = db->SPop(key);
  if (!member.has_value()) {
    return CommandResult(RespValue());  // nil
  }

  // Log to AOF
  std::array<absl::string_view, 1> aof_args = {key};
  context->LogToAof("SPOP", aof_args);

  return CommandResult(RespValue(std::move(*member)));
}

// SRANDMEMBER key [count]
CommandResult HandleSRandMember(const astra::protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'SRANDMEMBER' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();

  // For now, only support getting one random element
  auto member = db->SRandMember(key);
  if (!member.has_value()) {
    return CommandResult(RespValue());  // nil
  }

  return CommandResult(RespValue(std::move(*member)));
}

// SMOVE source destination member
CommandResult HandleSMove(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SMOVE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& source_arg = command[0];
  const auto& dest_arg = command[1];
  const auto& member_arg = command[2];

  if (!source_arg.IsBulkString() || !dest_arg.IsBulkString() ||
      !member_arg.IsBulkString()) {
    return CommandResult(false,
                         "ERR wrong type of arguments for 'SMOVE' command");
  }

  std::string source = source_arg.AsString();
  std::string destination = dest_arg.AsString();
  std::string member = member_arg.AsString();

  bool moved = db->SMove(source, destination, member);

  return CommandResult(RespValue(static_cast<int64_t>(moved ? 1 : 0)));
}

// SINTER key [key ...]
CommandResult HandleSInter(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SINTER' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  std::vector<std::string> keys;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  // Try to use WorkerScheduler for cross-shard query (NO SHARING architecture)
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    server::Worker* current_worker = context->GetWorker();
    
    // Collect all members from all sets across all workers
    std::vector<std::future<std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>>>> futures;
    futures.reserve(all_workers.size());
    
    for (size_t worker_id = 0; worker_id < all_workers.size(); ++worker_id) {
      auto promise = std::make_shared<std::promise<std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>>>>();
      auto future = promise->get_future();
      futures.push_back(std::move(future));
      
      server::Worker* target_worker = all_workers[worker_id];
      std::vector<std::string> keys_copy = keys;
      
      // Check if this is the current worker - execute directly to avoid deadlock
      if (target_worker == current_worker) {
        // Execute directly in current thread
        std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>> worker_sets;
        Database* db = &target_worker->GetDataShard().GetDatabase();
        
        for (const auto& key : keys_copy) {
          auto members = db->SMembers(key);
          worker_sets.push_back({key, absl::flat_hash_set<std::string>(members.begin(), members.end())});
        }
        
        promise->set_value(worker_sets);
      } else {
        // Execute on other worker via queue
        all_workers[worker_id]->AddTask([keys_copy, target_worker, promise]() {
          try {
            std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>> worker_sets;
            Database* db = &target_worker->GetDataShard().GetDatabase();
            
            for (const auto& key : keys_copy) {
              auto members = db->SMembers(key);
              worker_sets.push_back({key, absl::flat_hash_set<std::string>(members.begin(), members.end())});
            }
            
            promise->set_value(worker_sets);
          } catch (...) {
            promise->set_exception(std::current_exception());
          }
        });
        
        // Notify worker to process task immediately
        all_workers[worker_id]->NotifyTaskProcessing();
      }
    }
    
    // Aggregate results from all workers
    absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> all_sets;
    for (auto& future : futures) {
      auto worker_sets = future.get();
      for (const auto& [key, members] : worker_sets) {
        all_sets[key].insert(members.begin(), members.end());
      }
    }
    
    // Compute intersection
    if (all_sets.empty()) {
      RespValue result;
      result.SetArray({});
      return CommandResult(result);
    }
    
    absl::flat_hash_set<std::string> intersection = all_sets[keys[0]];
    for (size_t i = 1; i < keys.size(); ++i) {
      const auto& current_set = all_sets[keys[i]];
      absl::flat_hash_set<std::string> new_intersection;
      for (const auto& member : intersection) {
        if (current_set.find(member) != current_set.end()) {
          new_intersection.insert(member);
        }
      }
      intersection = std::move(new_intersection);
    }
    
    // Build response
    std::vector<RespValue> resp_members;
    resp_members.reserve(intersection.size());
    for (const auto& member : intersection) {
      resp_members.push_back(RespValue(member));
    }
    
    RespValue result;
    result.SetArray(std::move(resp_members));
    return CommandResult(result);
  }

  // Fallback: single worker mode
  auto members = db->SInter(keys);

  RespValue result;
  std::vector<RespValue> resp_members;
  resp_members.reserve(members.size());
  for (const auto& member : members) {
    resp_members.push_back(RespValue(member));
  }
  result.SetArray(std::move(resp_members));
  return CommandResult(result);
}

// SUNION key [key ...]
CommandResult HandleSUnion(const astra::protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SUNION' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  std::vector<std::string> keys;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  // Try to use WorkerScheduler for cross-shard query (NO SHARING architecture)
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    server::Worker* current_worker = context->GetWorker();
    
    // Collect all members from all sets across all workers
    std::vector<std::future<std::vector<absl::flat_hash_set<std::string>>>> futures;
    futures.reserve(all_workers.size());
    
    for (size_t worker_id = 0; worker_id < all_workers.size(); ++worker_id) {
      auto promise = std::make_shared<std::promise<std::vector<absl::flat_hash_set<std::string>>>>();
      auto future = promise->get_future();
      futures.push_back(std::move(future));
      
      server::Worker* target_worker = all_workers[worker_id];
      std::vector<std::string> keys_copy = keys;
      
      // Check if this is the current worker - execute directly to avoid deadlock
      if (target_worker == current_worker) {
        // Execute directly in current thread
        std::vector<absl::flat_hash_set<std::string>> worker_sets;
        Database* db = &target_worker->GetDataShard().GetDatabase();
        
        for (const auto& key : keys_copy) {
          auto members = db->SMembers(key);
          worker_sets.push_back(absl::flat_hash_set<std::string>(members.begin(), members.end()));
        }
        
        promise->set_value(worker_sets);
      } else {
        // Execute on other worker via queue
        all_workers[worker_id]->AddTask([keys_copy, target_worker, promise]() {
          try {
            std::vector<absl::flat_hash_set<std::string>> worker_sets;
            Database* db = &target_worker->GetDataShard().GetDatabase();
            
            for (const auto& key : keys_copy) {
              auto members = db->SMembers(key);
              worker_sets.push_back(absl::flat_hash_set<std::string>(members.begin(), members.end()));
            }
            
            promise->set_value(worker_sets);
          } catch (...) {
            promise->set_exception(std::current_exception());
          }
        });
        
        // Notify worker to process task immediately
        all_workers[worker_id]->NotifyTaskProcessing();
      }
    }
    
    // Aggregate union results from all workers
    absl::flat_hash_set<std::string> union_result;
    for (auto& future : futures) {
      auto worker_sets = future.get();
      for (const auto& worker_set : worker_sets) {
        union_result.insert(worker_set.begin(), worker_set.end());
      }
    }
    
    // Build response
    std::vector<RespValue> resp_members;
    resp_members.reserve(union_result.size());
    for (const auto& member : union_result) {
      resp_members.push_back(RespValue(member));
    }
    
    RespValue result;
    result.SetArray(std::move(resp_members));
    return CommandResult(result);
  }

  // Fallback: single worker mode
  auto members = db->SUnion(keys);

  RespValue result;
  std::vector<RespValue> resp_members;
  resp_members.reserve(members.size());
  for (const auto& member : members) {
    resp_members.push_back(RespValue(member));
  }
  result.SetArray(std::move(resp_members));
  return CommandResult(result);
}

// SDIFF key [key ...]
CommandResult HandleSDiff(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SDIFF' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  std::vector<std::string> keys;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  // Try to use WorkerScheduler for cross-shard query (NO SHARING architecture)
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    server::Worker* current_worker = context->GetWorker();
    
    // Collect all members from all sets across all workers
    std::vector<std::future<std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>>>> futures;
    futures.reserve(all_workers.size());
    
    for (size_t worker_id = 0; worker_id < all_workers.size(); ++worker_id) {
      auto promise = std::make_shared<std::promise<std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>>>>();
      auto future = promise->get_future();
      futures.push_back(std::move(future));
      
      server::Worker* target_worker = all_workers[worker_id];
      std::vector<std::string> keys_copy = keys;
      
      // Check if this is the current worker - execute directly to avoid deadlock
      if (target_worker == current_worker) {
        // Execute directly in current thread
        std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>> worker_sets;
        Database* db = &target_worker->GetDataShard().GetDatabase();
        
        for (const auto& key : keys_copy) {
          auto members = db->SMembers(key);
          worker_sets.push_back({key, absl::flat_hash_set<std::string>(members.begin(), members.end())});
        }
        
        promise->set_value(worker_sets);
      } else {
        // Execute on other worker via queue
        all_workers[worker_id]->AddTask([keys_copy, target_worker, promise]() {
          try {
            std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>> worker_sets;
            Database* db = &target_worker->GetDataShard().GetDatabase();
            
            for (const auto& key : keys_copy) {
              auto members = db->SMembers(key);
              worker_sets.push_back({key, absl::flat_hash_set<std::string>(members.begin(), members.end())});
            }
            
            promise->set_value(worker_sets);
          } catch (...) {
            promise->set_exception(std::current_exception());
          }
        });
        
        // Notify worker to process task immediately
        all_workers[worker_id]->NotifyTaskProcessing();
      }
    }
    
    // Aggregate results from all workers
    absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> all_sets;
    for (auto& future : futures) {
      auto worker_sets = future.get();
      for (const auto& [key, members] : worker_sets) {
        all_sets[key].insert(members.begin(), members.end());
      }
    }
    
    // Compute difference: first set minus all other sets
    if (all_sets.empty()) {
      RespValue result;
      result.SetArray({});
      return CommandResult(result);
    }
    
    absl::flat_hash_set<std::string> difference = all_sets[keys[0]];
    for (size_t i = 1; i < keys.size(); ++i) {
      const auto& current_set = all_sets[keys[i]];
      absl::flat_hash_set<std::string> new_difference;
      for (const auto& member : difference) {
        if (current_set.find(member) == current_set.end()) {
          new_difference.insert(member);
        }
      }
      difference = std::move(new_difference);
    }
    
    // Build response
    std::vector<RespValue> resp_members;
    resp_members.reserve(difference.size());
    for (const auto& member : difference) {
      resp_members.push_back(RespValue(member));
    }
    
    RespValue result;
    result.SetArray(std::move(resp_members));
    return CommandResult(result);
  }

  // Fallback: single worker mode
  auto members = db->SDiff(keys);

  RespValue result;
  std::vector<RespValue> resp_members;
  resp_members.reserve(members.size());
  for (const auto& member : members) {
    resp_members.push_back(RespValue(member));
  }
  result.SetArray(std::move(resp_members));
  return CommandResult(result);
}

// SINTERSTORE destination key [key ...]
CommandResult HandleSInterStore(const astra::protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'SINTERSTORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& dest_arg = command[0];
  if (!dest_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of destination argument");
  }

  std::string destination = dest_arg.AsString();

  std::vector<std::string> keys;
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  size_t count = db->SInterStore(destination, keys);

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// SUNIONSTORE destination key [key ...]
CommandResult HandleSUnionStore(const astra::protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'SUNIONSTORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& dest_arg = command[0];
  if (!dest_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of destination argument");
  }

  std::string destination = dest_arg.AsString();

  std::vector<std::string> keys;
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  size_t count = db->SUnionStore(destination, keys);

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// SDIFFSTORE destination key [key ...]
CommandResult HandleSDiffStore(const astra::protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'SDIFFSTORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& dest_arg = command[0];
  if (!dest_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of destination argument");
  }

  std::string destination = dest_arg.AsString();

  std::vector<std::string> keys;
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(arg.AsString());
  }

  size_t count = db->SDiffStore(destination, keys);

  return CommandResult(RespValue(static_cast<int64_t>(count)));
}

// SINTERCARD numkeys key [key ...] [LIMIT limit] - Return the cardinality of
// the intersection
CommandResult HandleSInterCard(const astra::protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'SINTERCARD' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& numkeys_arg = command[0];
  if (!numkeys_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of numkeys argument");
  }

  int64_t numkeys;
  if (!absl::SimpleAtoi(numkeys_arg.AsString(), &numkeys) || numkeys <= 0) {
    return CommandResult(false, "ERR value is not an integer or out of range");
  }

  if (command.ArgCount() < static_cast<size_t>(numkeys + 1)) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'SINTERCARD' command");
  }

  std::vector<std::string> keys;
  for (int64_t i = 0; i < numkeys; ++i) {
    const auto& key_arg = command[static_cast<size_t>(i) + 1];
    if (!key_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of key argument");
    }
    keys.push_back(key_arg.AsString());
  }

  // Parse LIMIT option
  size_t limit = 0;
  size_t pos = static_cast<size_t>(numkeys) + 1;
  while (pos < command.ArgCount()) {
    const auto& opt_arg = command[pos];
    if (!opt_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of option argument");
    }

    std::string opt = opt_arg.AsString();
    if (opt == "LIMIT" && pos + 1 < command.ArgCount()) {
      ++pos;
      int64_t limit_val;
      if (!absl::SimpleAtoi(command[pos].AsString(), &limit_val) ||
          limit_val < 0) {
        return CommandResult(false, "ERR limit is not a valid integer");
      }
      limit = static_cast<size_t>(limit_val);
    }
    ++pos;
  }

  // Try to use WorkerScheduler for cross-shard query (NO SHARING architecture)
  auto* worker_scheduler = context->GetWorkerScheduler();
  if (worker_scheduler && worker_scheduler->size() > 1) {
    auto all_workers = worker_scheduler->GetAllWorkers();
    server::Worker* current_worker = context->GetWorker();
    
    // Collect all members from all sets across all workers
    std::vector<std::future<std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>>>> futures;
    futures.reserve(all_workers.size());
    
    for (size_t worker_id = 0; worker_id < all_workers.size(); ++worker_id) {
      auto promise = std::make_shared<std::promise<std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>>>>();
      auto future = promise->get_future();
      futures.push_back(std::move(future));
      
      server::Worker* target_worker = all_workers[worker_id];
      std::vector<std::string> keys_copy = keys;
      
      // Check if this is the current worker - execute directly to avoid deadlock
      if (target_worker == current_worker) {
        // Execute directly in current thread
        std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>> worker_sets;
        Database* db = &target_worker->GetDataShard().GetDatabase();
        
        for (const auto& key : keys_copy) {
          auto members = db->SMembers(key);
          worker_sets.push_back({key, absl::flat_hash_set<std::string>(members.begin(), members.end())});
        }
        
        promise->set_value(worker_sets);
      } else {
        // Execute on other worker via queue
        all_workers[worker_id]->AddTask([keys_copy, target_worker, promise]() {
          try {
            std::vector<std::pair<std::string, absl::flat_hash_set<std::string>>> worker_sets;
            Database* db = &target_worker->GetDataShard().GetDatabase();
            
            for (const auto& key : keys_copy) {
              auto members = db->SMembers(key);
              worker_sets.push_back({key, absl::flat_hash_set<std::string>(members.begin(), members.end())});
            }
            
            promise->set_value(worker_sets);
          } catch (...) {
            promise->set_exception(std::current_exception());
          }
        });
        
        // Notify worker to process task immediately
        all_workers[worker_id]->NotifyTaskProcessing();
      }
    }
    
    // Aggregate results from all workers
    absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> all_sets;
    for (auto& future : futures) {
      auto worker_sets = future.get();
      for (const auto& [key, members] : worker_sets) {
        all_sets[key].insert(members.begin(), members.end());
      }
    }
    
    // Compute intersection
    if (all_sets.empty()) {
      return CommandResult(RespValue(static_cast<int64_t>(0)));
    }
    
    absl::flat_hash_set<std::string> intersection = all_sets[keys[0]];
    for (size_t i = 1; i < keys.size(); ++i) {
      const auto& current_set = all_sets[keys[i]];
      absl::flat_hash_set<std::string> new_intersection;
      for (const auto& member : intersection) {
        if (current_set.find(member) != current_set.end()) {
          new_intersection.insert(member);
        }
      }
      intersection = std::move(new_intersection);
      
      // Early exit if intersection is empty
      if (intersection.empty()) {
        break;
      }
    }
    
    // Apply LIMIT
    if (limit > 0 && intersection.size() > limit) {
      return CommandResult(RespValue(static_cast<int64_t>(limit)));
    }
    
    return CommandResult(RespValue(static_cast<int64_t>(intersection.size())));
  }

  // Fallback: single worker mode
  // Compute intersection
  auto intersection = db->SInter(keys);

  // Apply LIMIT
  if (limit > 0 && intersection.size() > limit) {
    intersection.resize(limit);
  }

  return CommandResult(RespValue(static_cast<int64_t>(intersection.size())));
}

// SMISMEMBER key member [member ...] - Check if multiple members are in a set
CommandResult HandleSMIsMember(const astra::protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'SMISMEMBER' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();

  std::vector<RespValue> result;
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& member_arg = command[i];
    if (!member_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of member argument");
    }

    bool is_member = db->SIsMember(key, member_arg.AsString());
    result.emplace_back(RespValue(static_cast<int64_t>(is_member ? 1 : 0)));
  }

  return CommandResult(RespValue(std::move(result)));
}

// SORT key [BY pattern] [LIMIT offset count] [GET pattern [GET pattern ...]]
// [ASC|DESC] [ALPHA] [STORE destination]
CommandResult HandleSort(const astra::protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SORT' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();

  // Get all elements (simplified implementation)
  auto key_type = db->GetType(key);
  if (!key_type.has_value()) {
    return CommandResult(RespValue(std::vector<RespValue>{}));
  }

  // Parse options
  bool alpha = false;
  bool reverse = false;
  std::string store_destination;
  size_t limit_offset = 0;
  size_t limit_count = -1;

  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of option argument");
    }

    std::string opt = arg.AsString();
    if (opt == "ASC") {
      reverse = false;
    } else if (opt == "DESC") {
      reverse = true;
    } else if (opt == "ALPHA") {
      alpha = true;
    } else if (opt == "LIMIT" && i + 2 < command.ArgCount()) {
      int64_t offset_val, count_val;
      if (!absl::SimpleAtoi(command[i + 1].AsString(), &offset_val) ||
          offset_val < 0) {
        return CommandResult(false, "ERR offset is not a valid integer");
      }
      if (!absl::SimpleAtoi(command[i + 2].AsString(), &count_val) ||
          count_val < 0) {
        return CommandResult(false, "ERR count is not a valid integer");
      }
      limit_offset = static_cast<size_t>(offset_val);
      limit_count = static_cast<size_t>(count_val);
      i += 2;
    } else if (opt == "STORE" && i + 1 < command.ArgCount()) {
      store_destination = command[++i].AsString();
    } else if (opt == "BY" || opt == "GET") {
      // Skip pattern (simplified implementation)
      i++;
    }
  }

  // Get elements based on type
  std::vector<std::string> elements;
  if (key_type.value() == astra::storage::KeyType::kSet) {
    elements = db->SMembers(key);
  } else if (key_type.value() == astra::storage::KeyType::kList) {
    elements = db->LRange(key, 0, -1);
  } else if (key_type.value() == astra::storage::KeyType::kZSet) {
    auto zset_elements = db->ZRangeByRank(key, 0, -1, false, false);
    for (const auto& [member, _] : zset_elements) {
      elements.push_back(member);
    }
  } else {
    return CommandResult(
        false,
        "WRONGTYPE Operation against a key holding the wrong kind of value");
  }

  // Sort elements
  if (alpha) {
    if (reverse) {
      std::sort(elements.begin(), elements.end(), std::greater<std::string>());
    } else {
      std::sort(elements.begin(), elements.end());
    }
  } else {
    // Try numeric sort (simplified - treat as strings for now)
    if (reverse) {
      std::sort(elements.begin(), elements.end(), std::greater<std::string>());
    } else {
      std::sort(elements.begin(), elements.end());
    }
  }

  // Apply LIMIT
  std::vector<std::string> result_elements;
  size_t start = std::min(limit_offset, elements.size());
  size_t end = std::min(start + limit_count, elements.size());
  for (size_t i = start; i < end; ++i) {
    result_elements.push_back(elements[i]);
  }

  // Build response
  if (!store_destination.empty()) {
    // Store result in destination key
    db->Del({store_destination});
    for (const auto& elem : result_elements) {
      db->SAdd(store_destination, elem);
    }
    return CommandResult(
        RespValue(static_cast<int64_t>(result_elements.size())));
  } else {
    std::vector<RespValue> resp;
    for (const auto& elem : result_elements) {
      resp.emplace_back(RespValue(elem));
    }
    return CommandResult(RespValue(std::move(resp)));
  }
}

// SORT_RO key [BY pattern] [LIMIT offset count] [GET pattern [GET pattern ...]]
// [ASC|DESC] [ALPHA] - Read-only sort
CommandResult HandleSortRo(const astra::protocol::Command& command,
                           CommandContext* context) {
  // For now, SORT_RO is the same as SORT
  return HandleSort(command, context);
}

// SPUBLISH channel message - Publish message to shard channel
CommandResult HandleSPublish(const astra::protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'SPUBLISH' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& channel_arg = command[0];
  const auto& message_arg = command[1];

  if (!channel_arg.IsBulkString() || !message_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string channel = channel_arg.AsString();
  std::string message = message_arg.AsString();

  // For now, SPUBLISH behaves like PUBLISH (can be optimized for shard
  // channels)
  auto* pubsub = context->GetPubSubManager();
  if (!pubsub) {
    return CommandResult(false, "ERR pubsub not initialized");
  }

  int64_t receivers = pubsub->Publish(channel, message);

  return CommandResult(RespValue(receivers));
}

// SSCAN key cursor [MATCH pattern] [COUNT count] - Incrementally iterate set
// elements
CommandResult HandleSScan(const astra::protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'SSCAN' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& cursor_arg = command[1];

  if (!key_arg.IsBulkString() || !cursor_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string cursor_str = cursor_arg.AsString();

  uint64_t cursor;
  if (!absl::SimpleAtoi(cursor_str, &cursor)) {
    return CommandResult(false, "ERR invalid cursor");
  }

  // Get all members
  auto all_members = db->SMembers(key);

  // Parse options
  std::string pattern = "*";
  size_t count = 10;

  for (size_t i = 2; i < command.ArgCount(); ++i) {
    const auto& arg = command[i];
    if (!arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of option argument");
    }

    std::string opt = arg.AsString();
    if (opt == "MATCH" && i + 1 < command.ArgCount()) {
      pattern = command[++i].AsString();
    } else if (opt == "COUNT" && i + 1 < command.ArgCount()) {
      if (!absl::SimpleAtoi(command[++i].AsString(), &count)) {
        return CommandResult(false,
                             "ERR value is not an integer or out of range");
      }
    }
  }

  // Filter members by pattern
  std::vector<std::string> matched_members;
  for (const auto& member : all_members) {
    bool matches = false;
    if (pattern == "*") {
      matches = true;
    } else if (pattern[0] == '*' && pattern.back() == '*' &&
               pattern.size() > 1) {
      // *middle* - contains
      std::string middle = pattern.substr(1, pattern.size() - 2);
      matches = (member.find(middle) != std::string::npos);
    } else if (pattern[0] == '*' && pattern.size() > 1) {
      // *suffix - ends with
      std::string suffix = pattern.substr(1);
      matches = (member.size() >= suffix.size() &&
                 member.substr(member.size() - suffix.size()) == suffix);
    } else if (pattern.back() == '*' && pattern.size() > 1) {
      // prefix* - starts with
      std::string prefix = pattern.substr(0, pattern.size() - 1);
      matches = (member.size() >= prefix.size() &&
                 member.substr(0, prefix.size()) == prefix);
    } else {
      // exact match
      matches = (member == pattern);
    }

    if (matches) {
      matched_members.push_back(member);
    }
  }

  // Get current page
  std::vector<RespValue> result_array;
  size_t start = static_cast<size_t>(cursor);
  size_t end = std::min(start + count, matched_members.size());

  for (size_t i = start; i < end; ++i) {
    result_array.emplace_back(RespValue(matched_members[i]));
  }

  // Build response
  std::vector<RespValue> response;

  // New cursor
  uint64_t new_cursor = (end >= matched_members.size()) ? 0 : end;
  RespValue cursor_val;
  cursor_val.SetString(std::to_string(new_cursor), RespType::kBulkString);
  response.emplace_back(cursor_val);

  // Results
  response.emplace_back(RespValue(std::move(result_array)));

  return CommandResult(RespValue(std::move(response)));
}

// SSUBSCRIBE channel [channel ...] - Subscribe to shard channels
CommandResult HandleSSubscribe(const astra::protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'SSUBSCRIBE' command");
  }

  auto* pubsub = context->GetPubSubManager();
  if (!pubsub) {
    return CommandResult(false, "ERR pubsub not initialized");
  }

  std::vector<RespValue> result;
  std::vector<std::string> channels;

  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& channel_arg = command[i];
    if (!channel_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of channel argument");
    }

    std::string channel = channel_arg.AsString();
    channels.push_back(channel);
  }

  uint64_t client_id = context->GetConnectionId();
  pubsub->Subscribe(client_id, channels);

  for (const auto& channel : channels) {
    RespValue subscribe_msg;
    subscribe_msg.SetString("ssubscribe", RespType::kBulkString);
    result.emplace_back(subscribe_msg);

    RespValue channel_val;
    channel_val.SetString(channel, RespType::kBulkString);
    result.emplace_back(channel_val);

    result.emplace_back(RespValue(static_cast<int64_t>(1)));
  }

  return CommandResult(RespValue(std::move(result)));
}

// SUNSUBSCRIBE channel [channel ...] - Unsubscribe from shard channels
CommandResult HandleSUnsubscribe(const astra::protocol::Command& command,
                                 CommandContext* context) {
  auto* pubsub = context->GetPubSubManager();
  if (!pubsub) {
    return CommandResult(false, "ERR pubsub not initialized");
  }

  std::vector<std::string> channels;
  if (command.ArgCount() == 0) {
    // Unsubscribe from all channels
    // TODO: Need to get all subscribed channels for client
    return CommandResult(RespValue(std::vector<RespValue>{}));
  }

  std::vector<RespValue> result;
  std::vector<std::string> unsubscribe_channels;

  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const auto& channel_arg = command[i];
    if (!channel_arg.IsBulkString()) {
      return CommandResult(false, "ERR wrong type of channel argument");
    }

    std::string channel = channel_arg.AsString();
    unsubscribe_channels.push_back(channel);
  }

  uint64_t client_id = context->GetConnectionId();
  pubsub->Unsubscribe(client_id, unsubscribe_channels);

  for (const auto& channel : unsubscribe_channels) {
    RespValue unsubscribe_msg;
    unsubscribe_msg.SetString("sunsubscribe", RespType::kBulkString);
    result.emplace_back(unsubscribe_msg);

    RespValue channel_val;
    channel_val.SetString(channel, RespType::kBulkString);
    result.emplace_back(channel_val);

    result.emplace_back(RespValue(static_cast<int64_t>(0)));
  }

  return CommandResult(RespValue(std::move(result)));
}

// Auto-register all set commands
ASTRADB_REGISTER_COMMAND(SADD, -3, "write", RoutingStrategy::kByFirstKey,
                         HandleSAdd);
ASTRADB_REGISTER_COMMAND(SREM, -3, "write", RoutingStrategy::kByFirstKey,
                         HandleSRem);
ASTRADB_REGISTER_COMMAND(SMEMBERS, 2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleSMembers);
ASTRADB_REGISTER_COMMAND(SISMEMBER, 3, "readonly", RoutingStrategy::kByFirstKey,
                         HandleSIsMember);
ASTRADB_REGISTER_COMMAND(SCARD, 2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleSCard);
ASTRADB_REGISTER_COMMAND(SMOVE, 3, "write", RoutingStrategy::kByFirstKey,
                         HandleSMove);
ASTRADB_REGISTER_COMMAND(SINTER, -2, "readonly", RoutingStrategy::kNone,
                         HandleSInter);
ASTRADB_REGISTER_COMMAND(SUNION, -2, "readonly", RoutingStrategy::kNone,
                         HandleSUnion);
ASTRADB_REGISTER_COMMAND(SDIFF, -2, "readonly", RoutingStrategy::kNone,
                         HandleSDiff);
ASTRADB_REGISTER_COMMAND(SINTERSTORE, -3, "write", RoutingStrategy::kByFirstKey,
                         HandleSInterStore);
ASTRADB_REGISTER_COMMAND(SUNIONSTORE, -3, "write", RoutingStrategy::kByFirstKey,
                         HandleSUnionStore);
ASTRADB_REGISTER_COMMAND(SDIFFSTORE, -3, "write", RoutingStrategy::kByFirstKey,
                         HandleSDiffStore);
ASTRADB_REGISTER_COMMAND(SPOP, 2, "write", RoutingStrategy::kByFirstKey,
                         HandleSPop);
ASTRADB_REGISTER_COMMAND(SRANDMEMBER, 2, "readonly",
                         RoutingStrategy::kByFirstKey, HandleSRandMember);
ASTRADB_REGISTER_COMMAND(SINTERCARD, -2, "readonly", RoutingStrategy::kNone,
                         HandleSInterCard);
ASTRADB_REGISTER_COMMAND(SMISMEMBER, -2, "readonly",
                         RoutingStrategy::kByFirstKey, HandleSMIsMember);
ASTRADB_REGISTER_COMMAND(SORT, -2, "write", RoutingStrategy::kByFirstKey,
                         HandleSort);
ASTRADB_REGISTER_COMMAND(SORT_RO, -2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleSortRo);
ASTRADB_REGISTER_COMMAND(SPUBLISH, 3, "write", RoutingStrategy::kByFirstKey,
                         HandleSPublish);
ASTRADB_REGISTER_COMMAND(SSCAN, -3, "readonly", RoutingStrategy::kByFirstKey,
                         HandleSScan);
ASTRADB_REGISTER_COMMAND(SSUBSCRIBE, -2, "write", RoutingStrategy::kByFirstKey,
                         HandleSSubscribe);
ASTRADB_REGISTER_COMMAND(SUNSUBSCRIBE, -1, "write",
                         RoutingStrategy::kByFirstKey, HandleSUnsubscribe);

}  // namespace astra::commands
