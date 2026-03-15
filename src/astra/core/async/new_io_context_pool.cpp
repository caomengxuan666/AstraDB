// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "new_io_context_pool.hpp"

#include "astra/base/logging.hpp"

namespace astra::core::async {

NewIOContextPool::NewIOContextPool(size_t num_workers)
    : num_workers_(num_workers > 0 ? num_workers
                                   : std::thread::hardware_concurrency()),
      running_(false) {
  ASTRADB_LOG_INFO("Creating NewIOContextPool with {} workers", num_workers_);

  // Create io_contexts and work_guards
  for (size_t i = 0; i < num_workers_; ++i) {
    auto io_context = std::make_unique<asio::io_context>();
    auto work_guard = std::make_unique<
        asio::executor_work_guard<asio::io_context::executor_type>>(
        io_context->get_executor());

    io_contexts_.push_back(std::move(io_context));
    work_guards_.push_back(std::move(work_guard));
  }

  ASTRADB_LOG_INFO("NewIOContextPool created successfully");
}

NewIOContextPool::~NewIOContextPool() { Stop(); }

void NewIOContextPool::Start() {
  if (running_) {
    ASTRADB_LOG_WARN("NewIOContextPool already running");
    return;
  }

  ASTRADB_LOG_INFO("Starting NewIOContextPool with {} workers...", num_workers_);

  running_ = true;

  // Start worker threads
  for (size_t i = 0; i < num_workers_; ++i) {
    worker_threads_.emplace_back([this, i]() {
      ASTRADB_LOG_DEBUG("Worker {} thread started", i);
      WorkerLoop(i);
      ASTRADB_LOG_DEBUG("Worker {} thread exited", i);
    });
  }

  ASTRADB_LOG_INFO("NewIOContextPool started successfully");
}

void NewIOContextPool::Stop() {
  if (!running_) {
    return;
  }

  ASTRADB_LOG_INFO("Stopping NewIOContextPool...");
  running_ = false;

  // Stop accepting new connections
  StopAcceptors();

  // Destroy work guards to allow io_contexts to exit
  work_guards_.clear();

  // Stop all io_contexts
  for (auto& io_context : io_contexts_) {
    io_context->stop();
  }

  // Wait for all worker threads to finish
  for (auto& thread : worker_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  worker_threads_.clear();

  ASTRADB_LOG_INFO("NewIOContextPool stopped");
}

void NewIOContextPool::WorkerLoop(size_t worker_id) {
  io_contexts_[worker_id]->run();
}

void NewIOContextPool::StartAcceptor(size_t worker_id, const std::string& host,
                                    uint16_t port, bool reuse_port) {
  if (worker_id >= io_contexts_.size()) {
    ASTRADB_LOG_ERROR("Invalid worker_id: {}", worker_id);
    return;
  }

  if (worker_id < acceptors_.size() && acceptors_[worker_id]) {
    ASTRADB_LOG_WARN("Acceptor for worker {} already exists", worker_id);
    return;
  }

  // Without SO_REUSEPORT, only worker 0 can create the acceptor
  if (!reuse_port && worker_id != 0) {
    ASTRADB_LOG_INFO(
        "Worker {} skipped acceptor creation (only worker 0 accepts without "
        "SO_REUSEPORT)",
        worker_id);
    return;
  }

  asio::ip::tcp::endpoint endpoint(asio::ip::make_address(host), port);

  auto acceptor =
      std::make_unique<asio::ip::tcp::acceptor>(*io_contexts_[worker_id]);

  // Enable address reuse
  asio::error_code ec;
  acceptor->open(endpoint.protocol(), ec);
  if (ec) {
    throw std::runtime_error("Failed to open acceptor: " + ec.message());
  }

  acceptor->set_option(asio::socket_base::reuse_address(true), ec);
  if (ec) {
    throw std::runtime_error("Failed to set reuse_address: " + ec.message());
  }

  // Enable port reuse for multiple acceptors binding to the same address/port
  // ASIO's reuse_address option is cross-platform and handles platform differences
  // - Windows: Uses SO_REUSEADDR (allows binding to address/port in use)
  // - Linux: Uses SO_REUSEADDR (allows rebinding after TIME_WAIT)
  // - macOS/BSD: Uses SO_REUSEADDR
  //
  // On Linux, we additionally use SO_REUSEPORT for kernel-level load balancing
  // (allows multiple acceptors to share the same port with kernel distributing connections)
  if (reuse_port) {
#ifndef _WIN32
    // On Linux/Unix, try to enable SO_REUSEPORT for kernel-level load balancing
    // This allows multiple acceptors to bind to the same address/port
    // and the kernel will distribute connections evenly across them
    acceptor->set_option(
        asio::detail::socket_option::boolean<SOL_SOCKET, SO_REUSEPORT>(true),
        ec);
    if (ec) {
      ASTRADB_LOG_WARN(
          "Worker {}: SO_REUSEPORT not supported: {} (falling back to single "
          "acceptor mode)",
          worker_id, ec.message());
      if (worker_id == 0) {
        ASTRADB_LOG_INFO(
            "Falling back to single acceptor mode - connections will be "
            "accepted by worker 0 only");
      }
      // If SO_REUSEPORT fails, only worker 0 should continue
      if (worker_id != 0) {
        return;
      }
    } else if (worker_id == 0) {
      ASTRADB_LOG_INFO(
          "SO_REUSEPORT enabled - kernel will distribute connections evenly "
          "across {} workers",
          num_workers_);
    }
#else
    // On Windows, SO_REUSEPORT is not supported
    // Windows uses SO_REUSEADDR which is already set above
    // Note: Windows 10+ has SO_REUSE_UNICASTPORT but it's not directly available via ASIO
    ASTRADB_LOG_WARN(
        "Worker {}: SO_REUSEPORT not supported on Windows (using single acceptor mode)",
        worker_id);
    if (worker_id == 0) {
      ASTRADB_LOG_INFO(
          "Using single acceptor mode - connections will be accepted by worker 0 "
          "only");
    }
    // Only worker 0 should continue on Windows
    if (worker_id != 0) {
      return;
    }
#endif
  }

  // Bind
  acceptor->bind(endpoint, ec);
  if (ec) {
    // EADDRINUSE means the address is already in use (likely by another worker
    // with SO_REUSEPORT, which is expected)
    if (ec != asio::error::address_in_use || worker_id == 0) {
      throw std::runtime_error("Failed to bind acceptor: " + ec.message());
    }
  }

  // Listen (only on the first acceptor that successfully bound)
  if (worker_id == 0 || (reuse_port && ec != asio::error::address_in_use)) {
    acceptor->listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
      throw std::runtime_error("Failed to listen: " + ec.message());
    }
  }

  // Ensure acceptors_ has enough space
  if (acceptors_.size() <= worker_id) {
    acceptors_.resize(worker_id + 1);
  }
  acceptors_[worker_id] = std::move(acceptor);

  ASTRADB_LOG_INFO("Worker {} acceptor started on {}:{}", worker_id, host,
                   port);

  // Start accepting connections
  DoAccept(worker_id);
}

void NewIOContextPool::StopAcceptors() {
  for (auto& acceptor : acceptors_) {
    if (acceptor) {
      asio::error_code ec;
      acceptor->close(ec);
    }
  }
  acceptors_.clear();
}

void NewIOContextPool::DoAccept(size_t worker_id) {
  if (worker_id >= acceptors_.size() || !acceptors_[worker_id]) {
    return;
  }

  auto& acceptor = *acceptors_[worker_id];

  // Use a shared_ptr to manage the connection callback lifetime
  auto callback = std::make_shared<NewConnectionCallback>(connection_callback_);
  
  // Capture 'this' pointer directly - the pool outlives the worker threads
  // and we have work_guards_ to keep io_contexts alive
  acceptor.async_accept([callback, worker_id, this](asio::error_code ec,
                                         asio::ip::tcp::socket socket) {
    ASTRADB_LOG_INFO("Worker {}: Accept callback entered", worker_id);
    
    if (!ec) {
    
          ASTRADB_LOG_INFO("Worker {} accepted new connection", worker_id);
    
    
    
          // Check if running
      if (!running_) {
        ASTRADB_LOG_WARN("Worker {} pool not running, closing socket", worker_id);
        socket.close();
        return;
      }

      // Call the connection callback if set
      if (*callback) {
        ASTRADB_LOG_DEBUG("Worker {} calling connection_callback", worker_id);
        
        try {
          (*callback)(worker_id, std::move(socket));
          ASTRADB_LOG_DEBUG("Worker {} connection_callback returned", worker_id);
        } catch (const std::exception& e) {
          ASTRADB_LOG_ERROR("Worker {} connection_callback threw exception: {}", worker_id, e.what());
        }
      } else {
        ASTRADB_LOG_WARN(
            "No connection callback set, closing socket from worker {}",
            worker_id);
        socket.close();
      }
    } else if (running_) {
      ASTRADB_LOG_ERROR("Worker {} accept error: {}", worker_id,
                       ec.message());
    }

    // Continue accepting if still running
    if (running_) {
      ASTRADB_LOG_DEBUG("Worker {} continuing to accept", worker_id);
      DoAccept(worker_id);
    }
  });
}

}  // namespace astra::core::async