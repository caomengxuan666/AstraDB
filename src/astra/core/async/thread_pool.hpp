// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <asio.hpp>
#include <blockingconcurrentqueue.h>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>

namespace astra::core::async {

// IO Context Thread Pool - High-performance async task executor
// Uses asio::io_context with work-stealing via BlockingConcurrentQueue
class IOContextThreadPool {
 public:
  explicit IOContextThreadPool(size_t num_threads = 0);
  ~IOContextThreadPool();
  
  // Post work to the thread pool
  template <typename F>
  void Post(F&& work) {
    // Round-robin to different io_contexts for load balancing
    size_t index = next_index_.fetch_add(1, std::memory_order_relaxed) % io_contexts_.size();
    asio::post(*io_contexts_[index], std::forward<F>(work));
  }
  
  // Post work to a specific IO context
  template <typename F>
  void PostTo(size_t index, F&& work) {
    if (index >= io_contexts_.size()) {
      index = index % io_contexts_.size();
    }
    asio::post(*io_contexts_[index], std::forward<F>(work));
  }
  
  // Get number of IO contexts
  size_t Size() const { return io_contexts_.size(); }
  
  // Get IO context by index
  asio::io_context& Get(size_t index) {
    return *io_contexts_[index % io_contexts_.size()];
  }
  
  // Stop all threads
  void Stop();
  
 private:
  void WorkerLoop(size_t thread_id);
  
  std::vector<std::unique_ptr<asio::io_context>> io_contexts_;
  std::vector<std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>>> work_guards_;
  std::vector<std::thread> worker_threads_;
  std::atomic<size_t> next_index_{0};
  std::atomic<bool> running_{false};
};

// Global thread pool instance
IOContextThreadPool& GetGlobalThreadPool();

// Global function for posting to main IO context (defined in thread_pool.cpp)
extern std::function<void(std::function<void()>)> g_post_to_main_io_context_func;

// Simple helper for posting to main IO context (no thread pool overhead)
// This is the simplest implementation for testing baseline performance
inline void PostToMainIOContext(std::function<void()> work) {
  // This will be implemented by the Server class
  // For now, we use a global function
  extern void PostToMainIOContextImpl(std::function<void()>);
  PostToMainIOContextImpl(std::move(work));
}

}  // namespace astra::core::async