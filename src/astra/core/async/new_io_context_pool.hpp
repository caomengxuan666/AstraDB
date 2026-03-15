// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <asio.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#ifdef _WIN32
  // Windows
  #include <winsock2.h>
  #include <windows.h>
#else
  // Linux/Unix
  #include <sys/socket.h>
#endif

// SO_REUSEPORT option for Linux (not available on Windows)
#ifndef SO_REUSEPORT
  #ifdef _WIN32
    // Windows does not support SO_REUSEPORT
    #define SO_REUSEPORT 0
  #else
    #define SO_REUSEPORT 15
  #endif
#endif

namespace astra::core::async {

// Callback type for new connections
using NewConnectionCallback =
    std::function<void(size_t worker_id, asio::ip::tcp::socket socket)>;

// New IO Context Pool with SO_REUSEPORT support
// Implements per-worker event loop architecture with kernel-level load balancing
class NewIOContextPool : public std::enable_shared_from_this<NewIOContextPool> {
 public:
  explicit NewIOContextPool(size_t num_workers = 0);
  ~NewIOContextPool();

  // Start the pool (creates threads)
  void Start();

  // Stop all workers
  void Stop();

  // Get specific worker's IO context
  asio::io_context& GetWorkerIOContext(size_t worker_id) {
    return *io_contexts_[worker_id % io_contexts_.size()];
  }

  // Get number of workers
  size_t GetWorkerCount() const { return io_contexts_.size(); }

  // Compatibility: Get IO service (round-robin for posting work)
  asio::io_context& GetIOService() {
    size_t index = next_index_.fetch_add(1, std::memory_order_relaxed) %
                   io_contexts_.size();
    return *io_contexts_[index];
  }

  // Set callback for new connections (used by Server class)
  void SetConnectionCallback(NewConnectionCallback callback) {
    connection_callback_ = std::move(callback);
  }

  // Start acceptor with SO_REUSEPORT (called by Server class)
  void StartAcceptor(size_t worker_id, const std::string& host, uint16_t port,
                     bool reuse_port);

  // Stop all acceptors
  void StopAcceptors();

 private:
  void WorkerLoop(size_t worker_id);
  void DoAccept(size_t worker_id);

  std::vector<std::unique_ptr<asio::io_context>> io_contexts_;
  std::vector<std::unique_ptr<asio::ip::tcp::acceptor>> acceptors_;
  std::vector<std::thread> worker_threads_;
  std::vector<std::unique_ptr<
      asio::executor_work_guard<asio::io_context::executor_type>>>
      work_guards_;

  NewConnectionCallback connection_callback_;

  size_t num_workers_;
  std::atomic<size_t> next_index_{0};
  std::atomic<bool> running_{false};
};

}  // namespace astra::core::async