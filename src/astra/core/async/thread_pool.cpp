// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include <absl/functional/any_invocable.h>
#include "thread_pool.hpp"
#include <absl/functional/any_invocable.h>
#include "astra/base/logging.hpp"

namespace astra::core::async {

// Global function for posting to main IO context (defined in server.cpp)
absl::AnyInvocable<void(absl::AnyInvocable<void()>)> g_post_to_main_io_context_func;

void PostToMainIOContextImpl(absl::AnyInvocable<void()> work) {
  if (g_post_to_main_io_context_func) {
    g_post_to_main_io_context_func(std::move(work));
  }
}

void IOContextThreadPool::WorkerLoop(size_t thread_id) {
  ASTRADB_LOG_INFO("Thread pool worker {} started", thread_id);
  
  // Run the io_context - this blocks until stop() is called
  io_contexts_[thread_id]->run();
  
  ASTRADB_LOG_INFO("Thread pool worker {} exited", thread_id);
}

IOContextThreadPool::IOContextThreadPool(size_t num_threads) {
  if (num_threads == 0) {
    num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) {
      num_threads = 4;
    }
    // Limit to 12 threads to avoid excessive context switching
    if (num_threads > 12) {
      num_threads = 12;
    }
  }
  
  running_ = true;
  
  ASTRADB_LOG_INFO("Creating IO context thread pool with {} threads (using asio::io_context)", num_threads);
  
  io_contexts_.reserve(num_threads);
  work_guards_.reserve(num_threads);
  worker_threads_.reserve(num_threads);
  
  // Create IO contexts and work guards
  for (size_t i = 0; i < num_threads; ++i) {
    auto io_context = std::make_unique<asio::io_context>();
    auto work_guard = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
        io_context->get_executor());
    
    io_contexts_.push_back(std::move(io_context));
    work_guards_.push_back(std::move(work_guard));
  }
  
  // Create worker threads that run IO contexts
  for (size_t i = 0; i < num_threads; ++i) {
    worker_threads_.emplace_back([this, i]() {
      WorkerLoop(i);
    });
  }
  
  ASTRADB_LOG_INFO("IO context thread pool created successfully");
}

IOContextThreadPool::~IOContextThreadPool() {
  Stop();
}

void IOContextThreadPool::Stop() {
  if (!running_.exchange(false)) {
    return;
  }
  
  ASTRADB_LOG_INFO("Stopping IO context thread pool...");
  
  // Remove work guards
  for (auto& work_guard : work_guards_) {
    work_guard.reset();
  }
  
  // Stop all IO contexts
  for (auto& io_context : io_contexts_) {
    io_context->stop();
  }
  
  // Wait for all worker threads
  for (auto& thread : worker_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  
  ASTRADB_LOG_INFO("IO context thread pool stopped");
}

// Global thread pool instance
namespace {
  std::unique_ptr<IOContextThreadPool> g_global_thread_pool;
}

IOContextThreadPool& GetGlobalThreadPool() {
  if (!g_global_thread_pool) {
    g_global_thread_pool = std::make_unique<IOContextThreadPool>();
  }
  return *g_global_thread_pool;
}

}  // namespace astra::core::async