// Copyright 2025 AstraDB Authors. All rights reserved.
// Use of this source code is governed by the Apache 2.0 license.

#pragma once

#include <absl/base/thread_annotations.h>
#include <absl/functional/function_ref.h>
#include <absl/types/span.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

#include "asio/awaitable.hpp"
#include "astra/base/logging.hpp"
#include "astra/server/worker.hpp"

namespace astra::server {

class Worker;

/**
 * @brief Worker Scheduler - Central coordinator for cross-Worker task dispatch
 *
 * This class provides a Dragonfly-like scheduling mechanism for dispatching
 * tasks to specific Workers or all Workers. It maintains NO SHARING
 * architecture by:
 * - Only dispatching tasks, not sharing data
 * - Using thread-safe queues for communication
 * - Each Worker maintains its own task queue
 *
 * Design Principles:
 * - Use coroutines (asio::awaitable) when possible for cleaner code
 * - Use async operations when performance-critical
 * - Use Abseil containers for better performance
 *
 * Similar to Dragonfly's EngineShardSet but adapted for our NO SHARING
 * architecture.
 */
class WorkerScheduler {
 public:
  explicit WorkerScheduler(const std::vector<Worker*>& workers);
  ~WorkerScheduler() = default;

  // Disable copy and move
  WorkerScheduler(const WorkerScheduler&) = delete;
  WorkerScheduler& operator=(const WorkerScheduler&) = delete;
  WorkerScheduler(WorkerScheduler&&) = delete;
  WorkerScheduler& operator=(WorkerScheduler&&) = delete;

  /**
   * @brief Get number of workers
   */
  size_t size() const { return workers_.size(); }

  /**
   * @brief Add task to specific worker (async, non-blocking)
   *
   * @param worker_id Target worker ID
   * @param func Task to execute (must be copyable)
   * @return true if task was added, false if worker_id is invalid
   */
  template <typename F>
  bool Add(size_t worker_id, F&& func) {
    if (worker_id >= workers_.size()) {
      ASTRADB_LOG_ERROR("WorkerScheduler: Invalid worker_id {}", worker_id);
      return false;
    }
    ASTRADB_LOG_DEBUG("WorkerScheduler: Adding task to worker {}", worker_id);
    workers_[worker_id]->AddTask(std::forward<F>(func));
    workers_[worker_id]->NotifyTaskProcessing();
    return true;
  }

  /**
   * @brief Add task to specific worker and wait for result
   *
   * @param worker_id Target worker ID
   * @param func Task to execute (must be copyable)
   * @return Result of func(), or throws if worker_id is invalid
   */
  template <typename F>
  auto Await(size_t worker_id, F&& func) -> decltype(func()) {
    using ResultType = decltype(func());

    if (worker_id >= workers_.size()) {
      throw std::out_of_range("Invalid worker_id: " +
                              std::to_string(worker_id));
    }

    std::promise<ResultType> promise;
    auto future = promise.get_future();

    workers_[worker_id]->AddTask(
        [func = std::forward<F>(func), promise = std::move(promise)]() mutable {
          try {
            if constexpr (std::is_void_v<ResultType>) {
              func();
              promise.set_value();
            } else {
              promise.set_value(func());
            }
          } catch (...) {
            promise.set_exception(std::current_exception());
          }
        });
    workers_[worker_id]->NotifyTaskProcessing();

    return WaitFutureWithCallerPump(future, nullptr);
  }

  /**
   * @brief Run function on all workers in parallel and wait for completion
   *
   * @param func Function to run on each worker (must be copyable)
   */
  template <typename F>
  void RunOnAll(F&& func) {
    std::vector<std::future<void>> futures;
    futures.reserve(workers_.size());

    for (size_t i = 0; i < workers_.size(); ++i) {
      std::promise<void> promise;
      auto future = promise.get_future();
      futures.push_back(std::move(future));

      workers_[i]->AddTask([func, promise = std::move(promise)]() mutable {
        try {
          func();
          promise.set_value();
        } catch (...) {
          promise.set_exception(std::current_exception());
        }
      });
      workers_[i]->NotifyTaskProcessing();
    }

    // Wait for all tasks to complete
    for (auto& f : futures) {
      WaitFutureWithCallerPump(f, nullptr);
    }
  }

  /**
   * @brief Run function on all workers asynchronously (non-blocking)
   *
   * @param func Function to run on each worker (must be copyable)
   */
  template <typename F>
  void DispatchOnAll(F&& func) {
    for (size_t i = 0; i < workers_.size(); ++i) {
      workers_[i]->AddTask(func);
      workers_[i]->NotifyTaskProcessing();
    }
  }

  /**
   * @brief Run function on selected workers in parallel and wait
   *
   * @param func Function to run
   * @param predicate Predicate to select workers (bool(worker_id))
   */
  template <typename F, typename P>
  void RunOnSelected(F&& func, P&& predicate) {
    std::vector<std::future<void>> futures;

    for (size_t i = 0; i < workers_.size(); ++i) {
      if (!predicate(i)) {
        continue;
      }

      std::promise<void> promise;
      auto future = promise.get_future();
      futures.push_back(std::move(future));

      workers_[i]->AddTask([func, promise = std::move(promise)]() mutable {
        try {
          func();
          promise.set_value();
        } catch (...) {
          promise.set_exception(std::current_exception());
        }
      });
      workers_[i]->NotifyTaskProcessing();
    }

    for (auto& f : futures) {
      WaitFutureWithCallerPump(f, nullptr);
    }
  }

  /**
   * @brief Wait for a future while pumping caller worker scheduler tasks.
   *
   * This avoids cross-worker circular wait stalls in no-sharing architecture:
   * while caller is waiting on remote worker result, caller still executes
   * pending scheduler tasks.
   */
  template <typename T>
  static T WaitFutureWithCallerPump(std::future<T>& future, Worker* caller) {
    uint32_t idle_rounds = 0;
    while (future.wait_for(std::chrono::microseconds(0)) !=
           std::future_status::ready) {
      bool has_work = false;
      if (caller) {
        has_work = caller->ProcessPendingSchedulerTasksForCrossWorkerWait();
      }
      if (has_work) {
        idle_rounds = 0;
        continue;
      }
      ++idle_rounds;
      if (idle_rounds <= 256) {
        // Keep early waits in userspace to avoid sched_yield syscall overhead.
        RelaxCpu();
      } else if (idle_rounds <= 2048) {
        std::this_thread::sleep_for(std::chrono::microseconds(5));
      } else {
        std::this_thread::sleep_for(std::chrono::microseconds(20));
      }
    }
    return future.get();
  }

  static void WaitFutureWithCallerPump(std::future<void>& future,
                                       Worker* caller) {
    uint32_t idle_rounds = 0;
    while (future.wait_for(std::chrono::microseconds(0)) !=
           std::future_status::ready) {
      bool has_work = false;
      if (caller) {
        has_work = caller->ProcessPendingSchedulerTasksForCrossWorkerWait();
      }
      if (has_work) {
        idle_rounds = 0;
        continue;
      }
      ++idle_rounds;
      if (idle_rounds <= 256) {
        // Keep early waits in userspace to avoid sched_yield syscall overhead.
        RelaxCpu();
      } else if (idle_rounds <= 2048) {
        std::this_thread::sleep_for(std::chrono::microseconds(5));
      } else {
        std::this_thread::sleep_for(std::chrono::microseconds(20));
      }
    }
    future.get();
  }

  /**
   * @brief Get worker by ID
   */
  Worker* GetWorker(size_t worker_id) const {
    if (worker_id >= workers_.size()) {
      return nullptr;
    }
    return workers_[worker_id];
  }

  /**
   * @brief Get all workers
   */
  absl::Span<Worker* const> GetAllWorkers() const {
    return absl::MakeSpan(workers_);
  }

 private:
  static inline void RelaxCpu() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
  }

  std::vector<Worker*> workers_;
};

}  // namespace astra::server
