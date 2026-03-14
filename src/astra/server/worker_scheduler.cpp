// Copyright 2025 AstraDB Authors. All rights reserved.
// Use of this source code is governed by the Apache 2.0 license.

#include "worker_scheduler.hpp"

#include "worker.hpp"

namespace astra::server {

WorkerScheduler::WorkerScheduler(const std::vector<Worker*>& workers)
    : workers_(workers) {
  ASTRADB_LOG_INFO("WorkerScheduler: Initialized with {} workers", workers.size());
}

}  // namespace astra::server