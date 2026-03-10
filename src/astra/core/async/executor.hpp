// ==============================================================================
// Coroutine Executor
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/base/thread_annotations.h>
#include <absl/functional/any_invocable.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "astra/base/logging.hpp"
#include "astra/base/macros.hpp"

namespace astra::core::async {

// Forward declarations
class Executor;
class FiberManager;

// ==============================================================================
// Executor - Coroutine Execution Engine
// ==============================================================================
// Provides a unified interface for running coroutines with Asio
// Supports multi-threaded execution with per-thread event loops
// ==============================================================================

class Executor final {
 public:
  // Constructor
  // @param num_threads: Number of worker threads (0 = hardware concurrency)
  explicit Executor(size_t num_threads = 0);

  // Destructor - stops all worker threads
  ~Executor();

  // Disable copy and move
  ASTRABI_DISABLE_COPY_MOVE(Executor)

  // Start the executor
  void Run();

  // Stop the executor gracefully
  void Stop();

  // Post a task to the executor
  void Post(absl::AnyInvocable<void()> task);

  // Spawn a coroutine on the executor
  template <typename Awaitable>
  void Spawn(Awaitable&& awaitable, const char* name = nullptr);

  // Get the underlying io_context
  asio::io_context& GetIoContext() { return io_context_; }

  // Get the number of worker threads
  size_t GetThreadCount() const { return threads_.size(); }

  // Get the thread index for the current thread
  size_t GetThreadIndex() const;

 private:
  // Worker thread function
  void WorkerThread(size_t thread_index);

  asio::io_context io_context_;
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>>
      work_guard_;
  std::vector<std::thread> threads_;
  std::atomic<bool> running_{false};
  std::atomic<size_t> next_thread_index_{0};
};

// ==============================================================================
// Executor Implementation
// ==============================================================================

inline Executor::Executor(size_t num_threads) : io_context_(1) {
  if (num_threads == 0) {
    num_threads = std::thread::hardware_concurrency();
  }

  threads_.reserve(num_threads);
  ASTRADB_LOG_INFO("Executor initialized with {} threads", num_threads);
}

inline Executor::~Executor() { Stop(); }

inline void Executor::Run() {
  running_.store(true, std::memory_order_release);

  // Create work guard to keep io_context running
  work_guard_ = std::make_unique<
      asio::executor_work_guard<asio::io_context::executor_type>>(
      io_context_.get_executor());

  // Start worker threads
  for (size_t i = 0; i < threads_.capacity(); ++i) {
    threads_.emplace_back(&Executor::WorkerThread, this, i);
  }

  ASTRADB_LOG_INFO("Executor started with {} worker threads", threads_.size());
}

inline void Executor::Stop() {
  if (running_.exchange(false, std::memory_order_acq_rel)) {
    // Destroy work guard to allow io_context to stop
    work_guard_.reset();

    // Stop the io_context
    io_context_.stop();

    // Join all worker threads
    for (auto& thread : threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }

    threads_.clear();
    ASTRADB_LOG_INFO("Executor stopped");
  }
}

inline void Executor::Post(absl::AnyInvocable<void()> task) {
  asio::post(io_context_, std::move(task));
}

template <typename Awaitable>
inline void Executor::Spawn(Awaitable&& awaitable, const char* name) {
  ASTRADB_LOG_DEBUG("Posting coroutine to executor: {}", name);
  asio::co_spawn(io_context_, std::forward<Awaitable>(awaitable),
                 [name](std::exception_ptr e) {
                   if (e) {
                     try {
                       std::rethrow_exception(e);
                     } catch (const std::exception& ex) {
                       ASTRADB_LOG_ERROR("Coroutine '{}' failed: {}",
                                         name ? name : "unnamed", ex.what());
                     }
                   }
                 });
}

inline size_t Executor::GetThreadIndex() const {
  // Simple thread ID hashing for now
  // In production, use thread-local storage for exact mapping
  return std::hash<std::thread::id>{}(std::this_thread::get_id()) %
         threads_.size();
}

inline void Executor::WorkerThread(size_t thread_index) {
  ASTRADB_LOG_DEBUG("Worker thread {} entering io_context_.run()",
                    thread_index);
  ASTRADB_LOG_DEBUG("Worker thread {} started", thread_index);
  ASTRADB_LOG_DEBUG("Worker thread {} entering io_context_.run()",
                    thread_index);

  try {
    io_context_.run();
  } catch (const std::exception& ex) {
    ASTRADB_LOG_ERROR("Worker thread {} exception: {}", thread_index,
                      ex.what());
  }

  ASTRADB_LOG_DEBUG("Worker thread {} stopped", thread_index);
}

}  // namespace astra::core::async
