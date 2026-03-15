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
  
  // Windows port reuse options:
  // - SO_REUSE_UNICASTPORT (Windows 10/11): Best for unicast port reuse (like SO_REUSEPORT on Linux)
  // - SO_REUSEADDR: Legacy option, allows binding to address/port in use
  #ifndef SO_REUSE_UNICASTPORT
    #define SO_REUSE_UNICASTPORT 0x0004  // Defined in Mswsock.h for Windows 10+
  #endif
  
  // On Windows, prefer SO_REUSE_UNICASTPORT (if available), fallback to SO_REUSEADDR
  #define ASTRADB_REUSEPORT SO_REUSE_UNICASTPORT
#else
  // Linux/Unix
  #include <sys/socket.h>
  #ifndef SO_REUSEPORT
    #define SO_REUSEPORT 15
  #endif
  #define ASTRADB_REUSEPORT SO_REUSEPORT
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