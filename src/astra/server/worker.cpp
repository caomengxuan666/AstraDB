// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "worker.hpp"

#include "astra/replication/replication_manager.hpp"
#include "worker_scheduler.hpp"

namespace astra::server {

void Worker::PropagateCommandToAllSlaves(const astra::protocol::Command& cmd) {
  if (!worker_scheduler_) {
    ASTRADB_LOG_ERROR(
        "Worker {}: Cannot propagate command - worker_scheduler_ is null",
        worker_id_);
    return;
  }

  ASTRADB_LOG_DEBUG(
      "Worker {}: Broadcasting command '{}' to all workers for replication",
      worker_id_, cmd.name);

  // Use WorkerScheduler to broadcast to all workers (NO SHADING architecture)
  auto all_workers = worker_scheduler_->GetAllWorkers();
  for (size_t i = 0; i < all_workers.size(); ++i) {
    auto* target_worker = all_workers[i];
    if (target_worker) {
      ASTRADB_LOG_DEBUG(
          "Worker {}: Adding task to worker {} for command propagation",
          worker_id_, i);
      // Capture command by value to make lambda copyable
      target_worker->AddTask(
          [cmd_copy = cmd, target_worker, this_worker_id = worker_id_]() {
            ASTRADB_LOG_DEBUG("Worker {}: Executing PropagateCommand for '{}'",
                              this_worker_id, cmd_copy.name);
            auto* repl_mgr = target_worker->GetReplicationManager();
            if (repl_mgr &&
                repl_mgr->GetRole() == replication::ReplicationRole::kMaster) {
              repl_mgr->PropagateCommand(cmd_copy);
            }
          });
      target_worker->NotifyTaskProcessing();
    }
  }
}

}  // namespace astra::server
