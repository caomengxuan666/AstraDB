// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <asio.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "astra/base/concurrentqueue_wrapper.hpp"
#include "astra/base/logging.hpp"
#include "astra/commands/command_auto_register.hpp"
#include "astra/commands/command_handler.hpp"
#include "astra/commands/database.hpp"
#include "astra/core/metrics.hpp"
#include "astra/protocol/resp/resp_parser.hpp"
#include "astra/protocol/resp/resp_builder.hpp"
#include "astra/protocol/resp/resp_types.hpp"
#include "managers.hpp"

namespace astra::server {

// Forward declarations
class Worker;
class PersistenceManager;

// Simple CommandContext implementation for Worker
class WorkerCommandContext : public astra::commands::CommandContext {
 public:
  explicit WorkerCommandContext(astra::commands::Database* db) : db_(db) {}

  astra::commands::Database* GetDatabase() const override { return db_; }
  int GetDBIndex() const override { return 0; }
  void SetDBIndex(int index) override { (void)index; }
  bool IsAuthenticated() const override { return true; }
  void SetAuthenticated(bool auth) override { (void)auth; }

  // Set AOF callback (for persistence)
  void SetAofCallbackString(std::function<void(const std::string&)> callback) {
    aof_callback_ = std::move(callback);
  }

  // Set RDB save callback (for persistence)
  void SetRdbSaveCallback(std::function<std::string(bool)> callback) {
    rdb_save_callback_ = std::move(callback);
  }

  // Get RDB save callback
  const std::function<std::string(bool)>& GetRdbSaveCallback() const {
    return rdb_save_callback_;
  }

  // Log command to AOF
  void LogToAof(absl::string_view command, absl::Span<const absl::string_view> args) override {
    ASTRADB_LOG_DEBUG("LogToAof called: command={}, args={}", command, args.size());
    if (aof_callback_) {
      // Build RESP command string
      std::string cmd_str;
      cmd_str += "*";
      cmd_str += std::to_string(args.size() + 1);  // +1 for the command name
      cmd_str += "\r\n";
      cmd_str += "$";
      cmd_str += std::to_string(command.size());
      cmd_str += "\r\n";
      cmd_str.append(command.data(), command.size());
      cmd_str += "\r\n";

      for (size_t i = 0; i < args.size(); ++i) {
        cmd_str += "$";
        cmd_str += std::to_string(args[i].size());
        cmd_str += "\r\n";
        cmd_str.append(args[i].data(), args[i].size());
        cmd_str += "\r\n";
      }

      ASTRADB_LOG_DEBUG("LogToAof calling callback, cmd_str size={}", cmd_str.size());
      aof_callback_(cmd_str);
      ASTRADB_LOG_DEBUG("LogToAof callback completed");
    } else {
      ASTRADB_LOG_WARN("LogToAof called but aof_callback_ is null!");
    }
  }

 private:
  astra::commands::Database* db_;
  std::function<void(const std::string&)> aof_callback_;
  std::function<std::string(bool)> rdb_save_callback_;  // bool = background save
};

// DataShard - Contains a full Database instance
class DataShard {
 public:
  explicit DataShard(size_t shard_id) : shard_id_(shard_id), context_(&database_) {
    // Initialize command registry
    auto cmd_count = astra::commands::RuntimeCommandRegistry::Instance().GetCommandCount();
    ASTRADB_LOG_INFO("Shard {}: RuntimeCommandRegistry has {} commands", shard_id_, cmd_count);

    astra::commands::RuntimeCommandRegistry::Instance().ApplyToRegistry(registry_);

    auto registry_size = registry_.Size();
    ASTRADB_LOG_INFO("Shard {}: CommandRegistry now has {} commands", shard_id_, registry_size);
  }

  // Execute a command using the command registry
  std::string Execute(const astra::protocol::Command& command) {
    ASTRADB_LOG_DEBUG("Shard {}: Executing command: {}", shard_id_, command.name);
    ASTRADB_LOG_DEBUG("Shard {}: Command args count: {}", shard_id_, command.args.size());

    auto result = registry_.Execute(command, &context_);
    ASTRADB_LOG_DEBUG("Shard {}: Registry::Execute completed", shard_id_);
    ASTRADB_LOG_DEBUG("Shard {}: Command result success={}, type={}", shard_id_, result.success, static_cast<int>(result.response.GetType()));

    if (!result.success) {
      ASTRADB_LOG_ERROR("Shard {}: Command failed: {}", shard_id_, result.error);
      return astra::protocol::RespBuilder::BuildError(result.error);
    }

    ASTRADB_LOG_DEBUG("Shard {}: Calling RespBuilder::Build", shard_id_);
    auto response = astra::protocol::RespBuilder::Build(result.response);
    ASTRADB_LOG_DEBUG("Shard {}: RespBuilder::Build completed, len={}", shard_id_, response.size());

    return response;
  }

  // Get database reference (for setting callbacks)
  astra::commands::Database& GetDatabase() { return database_; }

  // Get database reference (const version for RDB serialization)
  const astra::commands::Database& GetDatabase() const { return database_; }

  // Get command context (for setting callbacks)
  WorkerCommandContext* GetCommandContext() { return &context_; }

 private:
  size_t shard_id_;
  astra::commands::Database database_;
  WorkerCommandContext context_;
  astra::commands::CommandRegistry registry_;
};

// Cross-worker request (forwarded from one worker to another)
struct CrossWorkerRequest {
  size_t source_worker_id;  // Which worker sent this request
  uint64_t conn_id;         // Connection ID on source worker
  astra::protocol::Command command;
};

// Cross-worker response (sent back to source worker)
struct CrossWorkerResponse {
  uint64_t conn_id;         // Connection ID on source worker
  std::string response;     // Response data
};

// Batch cross-worker request for multi-key commands
struct BatchCrossWorkerRequest {
  uint64_t req_id;  // Unique request ID for matching responses
  size_t source_worker_id;
  std::string cmd_type;  // "SINTER", "SUNION", "ZUNIONSTORE", etc.
  std::vector<std::string> keys;  // Keys this worker should process
  std::vector<std::string> args;  // Additional arguments (weights, aggregate, etc.)
  std::shared_ptr<std::promise<std::vector<std::string>>> result_promise;  // For async response
};

struct BatchCrossWorkerResponse {
  uint64_t req_id;  // Matching request ID
  std::vector<std::string> result;  // Result from this worker
};

// Command with connection ID
struct CommandWithConnId {
  uint64_t conn_id;
  astra::protocol::Command command;
  bool is_forwarded;  // True if this is a forwarded command from another worker
  size_t source_worker_id;  // Which worker sent this forwarded command (only valid if is_forwarded=true)
};

// Response with connection ID
struct ResponseWithConnId {
  uint64_t conn_id;
  std::string response;
};

// Worker - NO SHARING architecture with MPSC cross-worker communication
// Each worker is a completely independent "mini server"
class Worker {
 public:
  explicit Worker(size_t worker_id, const std::string& host, uint16_t port,
                 std::vector<Worker*> all_workers)
      : worker_id_(worker_id),
        io_context_(),
        acceptor_(io_context_),
        data_shard_(worker_id),
        all_workers_(std::move(all_workers)),
        running_(false) {
    
    ASTRADB_LOG_INFO("Worker {}: Creating", worker_id_);
    
    // Setup acceptor
    asio::ip::tcp::endpoint endpoint(asio::ip::make_address(host), port);
    
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    
    // Enable SO_REUSEPORT
    #ifdef SO_REUSEPORT
    int reuseport = 1;
    setsockopt(acceptor_.native_handle(), SOL_SOCKET, SO_REUSEPORT,
               &reuseport, sizeof(reuseport));
    #endif
    
    acceptor_.bind(endpoint);
    acceptor_.listen(asio::socket_base::max_listen_connections);
    
    ASTRADB_LOG_INFO("Worker {}: Acceptor created on {}:{}", worker_id_, host, port);
  }

  ~Worker() { Stop(); }

  // Start the worker (starts both IO and executor threads)
  void Start() {
    if (running_) {
      ASTRADB_LOG_WARN("Worker {} already running", worker_id_);
      return;
    }

    running_ = true;
    ASTRADB_LOG_INFO("Worker {}: Starting", worker_id_);

    // Set batch request callback in database
    data_shard_.GetDatabase().SetBatchRequestCallback(
        [this](const std::string& cmd_type,
               const std::vector<std::string>& keys,
               const std::vector<std::string>& args) -> std::vector<std::string> {
          return SendBatchRequest(cmd_type, keys, args);
        });

    // Start IO thread
    io_thread_ = std::thread([this]() {
      ASTRADB_LOG_INFO("Worker {}: IO thread started", worker_id_);
      DoAccept();
      ProcessResponseQueue();  // Start response queue processing
      io_context_.run();
      ASTRADB_LOG_INFO("Worker {}: IO thread exited", worker_id_);
    });

    // Start executor thread
    exec_thread_ = std::thread([this]() {
      ASTRADB_LOG_INFO("Worker {}: Executor thread started", worker_id_);
      ExecutorLoop();
      ASTRADB_LOG_INFO("Worker {}: Executor thread exited", worker_id_);
    });
  }

  // Stop the worker
  void Stop() {
    if (!running_) {
      return;
    }

    ASTRADB_LOG_INFO("Worker {}: Stopping", worker_id_);
    running_ = false;

    // Stop acceptor
    asio::error_code ec;
    acceptor_.close(ec);

    // Stop io_context
    io_context_.stop();

    // Wait for threads
    if (io_thread_.joinable()) {
      io_thread_.join();
    }

    // Executor thread will stop when running_ is false
    if (exec_thread_.joinable()) {
      exec_thread_.join();
    }

    ASTRADB_LOG_INFO("Worker {}: Stopped", worker_id_);
  }

  // Get the worker's io_context (for creating connections)
  asio::io_context& GetIOContext() { return io_context_; }

  // Get worker ID
  size_t GetWorkerId() const { return worker_id_; }

  // Set all workers reference (called by Server after all workers are created)
  void SetAllWorkers(const std::vector<Worker*>& all_workers) {
    all_workers_ = all_workers;
  }

  // Set persistence manager (called by Server after persistence is initialized)

    void SetPersistenceManager(void* persistence_manager) {

      ASTRADB_LOG_INFO("Worker {}: SetPersistenceManager called with ptr={}", worker_id_, persistence_manager);

  

      if (persistence_manager) {

        auto* pm = static_cast<PersistenceManager*>(persistence_manager);

  

        // Set AOF callback

        data_shard_.GetCommandContext()->SetAofCallbackString([pm](const std::string& command) {

          ASTRADB_LOG_DEBUG("AOF callback invoked, command size={}", command.size());

          pm->AppendCommand(command);

          ASTRADB_LOG_DEBUG("AOF callback completed");

        });

  

        // Set RDB save callback

        data_shard_.GetCommandContext()->SetRdbSaveCallback([this, pm](bool background) -> std::string {

          ASTRADB_LOG_INFO("Worker {}: RDB save requested, background={}", worker_id_, background);

  

          if (background) {

            // Background save

            bool success = pm->BackgroundSaveRdb(all_workers_);

            if (success) {

              return "OK";

            } else {

              // Check if already in progress

              if (pm->IsBgSaveInProgress()) {

                return "ALREADY_IN_PROGRESS";

              }

              return "ERR Background save failed";

            }

          } else {

            // Synchronous save

            bool success = pm->SaveRdb(all_workers_);

            if (success) {

              return "OK";

            } else {

              return "ERR Save failed";

            }

          }

        });

  

        ASTRADB_LOG_INFO("Worker {}: Persistence callbacks set successfully", worker_id_);

      } else {

        ASTRADB_LOG_WARN("Worker {}: SetPersistenceManager called with null ptr", worker_id_);

      }

    }

  // Process a cross-worker request (MPSC queue entry point)
  void ProcessCrossWorkerRequest(const CrossWorkerRequest& req) {
    ASTRADB_LOG_DEBUG("Worker {}: Processing cross-worker request from Worker {}",
                     worker_id_, req.source_worker_id);

    // Execute command on this worker's data shard
    std::string response = data_shard_.Execute(req.command);

    // Send response back to source worker
    if (req.source_worker_id < all_workers_.size()) {
      CrossWorkerResponse cross_resp{req.conn_id, response};
      all_workers_[req.source_worker_id]->EnqueueCrossWorkerResponse(cross_resp);
    }
  }

  // Enqueue a cross-worker response (called by other workers)
  void EnqueueCrossWorkerResponse(const CrossWorkerResponse& resp) {
    cross_worker_resp_queue_.enqueue(resp);
  }

  // Enqueue a cross-worker request (called by other workers)
  void EnqueueCrossWorkerRequest(const CrossWorkerRequest& req) {
    // Create a command with connection ID for the request
    CommandWithConnId cmd{req.conn_id, req.command, true, req.source_worker_id};  // Mark as forwarded
    cmd_queue_.enqueue(cmd);
  }
  
  // Send batch request to multiple workers (for multi-key commands)
  std::vector<std::string> SendBatchRequest(
      const std::string& cmd_type,
      const std::vector<std::string>& keys,
      const std::vector<std::string>& args = {});
  
  // Enqueue batch response
  void EnqueueBatchResponse(const BatchCrossWorkerResponse& resp) {
    batch_resp_queue_.enqueue(resp);
  }

  // Enqueue batch request (called by other workers)
  void EnqueueBatchRequest(const BatchCrossWorkerRequest& req) {
    batch_req_queue_.enqueue(req);
  }
  
  // Process batch request from another worker
  void ProcessBatchRequest(const BatchCrossWorkerRequest& req);
  
  // Process batch responses (called in executor loop)
  void ProcessBatchResponses();

  // ========== RDB Persistence Operations ==========

  // Serialize worker's data to RDB format
  // Returns a vector of (key, type, value, ttl_ms) tuples
  std::vector<std::tuple<std::string, astra::storage::KeyType, std::string, int64_t>>
  GetRdbData() const {
    std::vector<std::tuple<std::string, astra::storage::KeyType, std::string, int64_t>> data;

    // Iterate through all keys in the database
    data_shard_.GetDatabase().ForEachKey([&data](
        const std::string& key,
        astra::storage::KeyType type,
        const std::string& value,
        int64_t ttl_ms) {
      data.emplace_back(key, type, value, ttl_ms);
    });

    return data;
  }

  // Get key count for RDB file
  size_t GetRdbKeyCount() const {
    return data_shard_.GetDatabase().GetKeyCount();
  }

  // Get expired keys count for RDB file
  size_t GetRdbExpiredKeysCount() const {
    return data_shard_.GetDatabase().GetExpiredKeysCount();
  }

 private:
  void DoAccept() {
    if (!running_) {
      return;
    }

    acceptor_.async_accept([this](asio::error_code ec, asio::ip::tcp::socket socket) {
      if (!ec && running_) {
        ASTRADB_LOG_INFO("Worker {}: Accepted connection, fd: {}", worker_id_,
                         socket.native_handle());

        // Create connection ID
        uint64_t conn_id = next_conn_id_++;

        // Create connection
        auto conn = std::make_shared<Connection>(
            worker_id_, conn_id, std::move(socket), &cmd_queue_, &resp_queue_);

        connections_[conn_id] = conn;
        conn->Start();

        // Record connection in metrics
        astra::metrics::AstraMetrics::Instance().IncrementConnections();

        ASTRADB_LOG_INFO("Worker {}: Connection {} started, total connections: {}",
                         worker_id_, conn_id, connections_.size());
      }

      // Continue accepting
      DoAccept();
    });
  }

  // Connection class - belongs to one worker
  class Connection : public std::enable_shared_from_this<Connection> {
   public:
    Connection(size_t worker_id, uint64_t conn_id, asio::ip::tcp::socket socket,
               moodycamel::ConcurrentQueue<CommandWithConnId>* cmd_queue,
               moodycamel::ConcurrentQueue<ResponseWithConnId>* resp_queue)
        : worker_id_(worker_id),
          conn_id_(conn_id),
          socket_(std::move(socket)),
          cmd_queue_(cmd_queue),
          resp_queue_(resp_queue) {
      ASTRADB_LOG_DEBUG("Worker {}: Connection {} created", worker_id_, conn_id_);
    }

    ~Connection() {
      ASTRADB_LOG_DEBUG("Worker {}: Connection {} destroyed", worker_id_, conn_id_);
      // Record connection in metrics
      astra::metrics::AstraMetrics::Instance().DecrementConnections();
    }

    void Start() {
      ASTRADB_LOG_DEBUG("Worker {}: Connection {} starting", worker_id_, conn_id_);
      // Spawn coroutine for reading (similar to Dragonfly's Fiber per connection)
      asio::co_spawn(socket_.get_executor(), [self = shared_from_this()]() -> asio::awaitable<void> {
        co_await self->DoRead();
      }, asio::detached);
    }

    asio::ip::tcp::socket& GetSocket() {
      return socket_;
    }

    asio::awaitable<void> Send(const std::string& response) {
      ASTRADB_LOG_DEBUG("Worker {}: Connection {} sending response: {} (len={})",
                       worker_id_, conn_id_, response, response.size());

      asio::error_code ec;
      size_t bytes_written = co_await asio::async_write(
          socket_, asio::buffer(response),
          asio::redirect_error(asio::use_awaitable, ec));

      if (!ec) {
        ASTRADB_LOG_DEBUG("Worker {}: Connection {} response sent (bytes={})",
                         worker_id_, conn_id_, bytes_written);
      } else {
        ASTRADB_LOG_ERROR("Worker {}: Connection {} write error: {}",
                          worker_id_, conn_id_, ec.message());
      }
    }

   private:
    asio::awaitable<void> DoRead() {
      // Get the executor for this coroutine
      auto executor = co_await asio::this_coro::executor;

      while (true) {
        asio::error_code ec;
        size_t bytes_transferred = co_await socket_.async_read_some(
            asio::buffer(buffer_),
            asio::redirect_error(asio::use_awaitable, ec));

        if (ec) {
          ASTRADB_LOG_ERROR("Worker {}: Connection {} read error: {}",
                           worker_id_, conn_id_, ec.message());
          break;
        }

        ASTRADB_LOG_DEBUG("Worker {}: Connection {} received {} bytes",
                         worker_id_, conn_id_, bytes_transferred);

        // Append to receive buffer
        receive_buffer_.append(buffer_.data(), bytes_transferred);

        // Process commands (minimal parsing only)
        ProcessCommands();
      }

      ASTRADB_LOG_DEBUG("Worker {}: Connection {} read loop terminated",
                       worker_id_, conn_id_);
    }

    void ProcessCommands() {
      while (true) {
        // Check if we have a complete RESP value
        if (!astra::protocol::RespParser::HasCompleteValue(receive_buffer_)) {
          break;
        }

        // Parse the command
        std::string_view data_view(receive_buffer_);
        auto value_opt = astra::protocol::RespParser::Parse(data_view);

        if (!value_opt) {
          ASTRADB_LOG_ERROR("Worker {}: Connection {} failed to parse RESP",
                           worker_id_, conn_id_);
          // Send error via response queue
          SendResponseViaQueue("ERR invalid RESP protocol");
          break;
        }

        // Calculate how many bytes were consumed
        size_t consumed = receive_buffer_.size() - data_view.size();
        receive_buffer_.erase(0, consumed);

        // Parse command from RESP value
        auto command_opt = astra::protocol::RespParser::ParseCommand(*value_opt);
        if (!command_opt) {
          ASTRADB_LOG_ERROR("Worker {}: Connection {} failed to parse command",
                           worker_id_, conn_id_);
          SendResponseViaQueue("ERR invalid command format");
          continue;
        }

        // Enqueue command to executor thread (within same worker!)
        CommandWithConnId cmd{conn_id_, *command_opt, false, worker_id_};  // Not forwarded, source is self
        cmd_queue_->enqueue(cmd);
      }
    }

    void SendResponseViaQueue(const std::string& response) {
      // Send response via response queue (from IO thread)
      ResponseWithConnId resp{conn_id_, response};
      resp_queue_->enqueue(resp);
    }

    size_t worker_id_;
    uint64_t conn_id_;
    asio::ip::tcp::socket socket_;
    moodycamel::ConcurrentQueue<CommandWithConnId>* cmd_queue_;
    moodycamel::ConcurrentQueue<ResponseWithConnId>* resp_queue_;

    std::array<char, 1024> buffer_;
    std::string receive_buffer_;
  };

  // Calculate which worker should handle this key (consistent hashing)
  size_t RouteToWorker(const std::string& key) {
    if (key.empty()) {
      return worker_id_ % all_workers_.size();
    }
    // Simple hash-based routing
    uint64_t hash = std::hash<std::string>{}(key);
    return hash % all_workers_.size();
  }

  void ExecutorLoop() {
    while (running_) {
      // Process local commands first
      CommandWithConnId cmd;
      if (cmd_queue_.try_dequeue(cmd)) {
        ASTRADB_LOG_DEBUG("Worker {}: Executor processing command: {} for conn {} (forwarded={})",
                         worker_id_, cmd.command.name, cmd.conn_id, cmd.is_forwarded);

        if (cmd.is_forwarded) {
          // This is a forwarded command from another worker
          // Execute directly and send response back to source worker
          astra::metrics::CommandTimer timer(cmd.command.name);
          std::string response = data_shard_.Execute(cmd.command);
          CrossWorkerResponse cross_resp{cmd.conn_id, response};
          all_workers_[cmd.source_worker_id]->EnqueueCrossWorkerResponse(cross_resp);
        } else {
          // This is a new command from a client
          // Determine if this command should be forwarded to another worker
          // Check if command has a key argument
          size_t target_worker = worker_id_;
          if (!cmd.command.args.empty() && 
              (cmd.command.args[0].IsBulkString() || cmd.command.args[0].IsSimpleString())) {
            std::string key = cmd.command.args[0].AsString();
            target_worker = RouteToWorker(key);
          }

          if (target_worker == worker_id_) {
            // Handle locally
            astra::metrics::CommandTimer timer(cmd.command.name);
            std::string response = data_shard_.Execute(cmd.command);
            ResponseWithConnId resp{cmd.conn_id, response};
            resp_queue_.enqueue(resp);
          } else {
            // Forward to target worker (enqueue to avoid blocking)
            ASTRADB_LOG_DEBUG("Worker {}: Forwarding command to Worker {}",
                             worker_id_, target_worker);
            CrossWorkerRequest cross_req{
              worker_id_,  // source worker
              cmd.conn_id,
              cmd.command
            };
            // Enqueue to target worker to avoid blocking this worker's ExecutorLoop
            all_workers_[target_worker]->EnqueueCrossWorkerRequest(cross_req);
          }
        }
      }

      // Process cross-worker responses
      CrossWorkerResponse cross_resp;
      while (cross_worker_resp_queue_.try_dequeue(cross_resp)) {
        ResponseWithConnId resp{cross_resp.conn_id, cross_resp.response};
        resp_queue_.enqueue(resp);
      }

      // Process batch responses
      ProcessBatchResponses();

      // Process batch requests from other workers
      BatchCrossWorkerRequest batch_req;
      while (batch_req_queue_.try_dequeue(batch_req)) {
        ProcessBatchRequest(batch_req);
      }

      // Yield if no work
      std::this_thread::yield();
    }
  }

  void ProcessResponseQueue() {
    if (!running_) {
      return;
    }

    // Check response queue and send responses (process up to 100 responses)
    for (int i = 0; i < 100; ++i) {
      ResponseWithConnId resp;
      if (resp_queue_.try_dequeue(resp)) {
        auto it = connections_.find(resp.conn_id);
        if (it != connections_.end()) {
          // Spawn coroutine to send response (non-blocking)
          asio::co_spawn(
              it->second->GetSocket().get_executor(),
              [conn = it->second, response = resp.response]() -> asio::awaitable<void> {
                co_await conn->Send(response);
              },
              asio::detached);
        }
      } else {
        break;  // Queue is empty
      }
    }

    // Schedule next check
    response_timer_.expires_after(std::chrono::milliseconds(1));
    response_timer_.async_wait([this](asio::error_code ec) {
      if (!ec && running_) {
        ProcessResponseQueue();
      }
    });
  }

  size_t worker_id_;
  asio::io_context io_context_;
  asio::ip::tcp::acceptor acceptor_;
  DataShard data_shard_;

  // Worker's private queues (no sharing!)
  moodycamel::ConcurrentQueue<CommandWithConnId> cmd_queue_;
  moodycamel::ConcurrentQueue<ResponseWithConnId> resp_queue_;
  
  // Cross-worker communication queues (MPSC)
  moodycamel::ConcurrentQueue<CrossWorkerResponse> cross_worker_resp_queue_;
  
  // Batch request queues (for multi-key commands)
  moodycamel::ConcurrentQueue<BatchCrossWorkerRequest> batch_req_queue_;
  moodycamel::ConcurrentQueue<BatchCrossWorkerResponse> batch_resp_queue_;
  
  // Pending batch requests (for matching responses)
  std::unordered_map<uint64_t, std::shared_ptr<BatchCrossWorkerRequest>> pending_batch_reqs_;
  std::atomic<uint64_t> next_batch_req_id_{1};

  absl::flat_hash_map<uint64_t, std::shared_ptr<Connection>> connections_;

  // Reference to all workers for cross-worker communication
  std::vector<Worker*> all_workers_;

  asio::steady_timer response_timer_{io_context_};  // Timer for checking response queue

  std::thread io_thread_;
  std::thread exec_thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> next_conn_id_{0};
  std::mutex batch_mutex_;  // For pending_batch_reqs_ access
};

// ==============================================================================
// Batch Request Implementation (Inline)
// ==============================================================================

inline std::vector<std::string> Worker::SendBatchRequest(
    const std::string& cmd_type,
    const std::vector<std::string>& keys,
    const std::vector<std::string>& args) {
  // Group keys by worker ID
  absl::flat_hash_map<size_t, std::vector<std::string>> worker_keys;
  for (const auto& key : keys) {
    size_t target_worker = RouteToWorker(key);
    worker_keys[target_worker].push_back(key);
  }

  // If all keys are on this worker, process locally
  if (worker_keys.size() == 1 && worker_keys.begin()->first == worker_id_) {
    if (cmd_type == "SINTER") {
      return data_shard_.GetDatabase().SInterLocal(keys);
    }
    return {};
  }

// For cross-worker requests, send requests and wait for responses
  absl::InlinedVector<std::vector<std::string>, 4> all_results;
  absl::InlinedVector<std::future<std::vector<std::string>>, 4> futures;

  // Send requests to all workers
  for (const auto& [target_worker_id, sub_keys] : worker_keys) {
    if (target_worker_id == worker_id_) {
      // Local processing
      if (cmd_type == "SINTER") {
        all_results.push_back(data_shard_.GetDatabase().SInterLocal(sub_keys));
      }
    } else {
      // Cross-worker request
      uint64_t req_id = next_batch_req_id_++;
      auto req = std::make_shared<BatchCrossWorkerRequest>();
      req->req_id = req_id;
      req->source_worker_id = worker_id_;
      req->cmd_type = cmd_type;
      req->keys = sub_keys;
      req->args = args;
      req->result_promise = std::make_shared<std::promise<std::vector<std::string>>>();

      // Store the request in pending map
      {
        std::lock_guard<std::mutex> lock(batch_mutex_);
        pending_batch_reqs_[req_id] = req;
      }

      // Get future before sending request
      futures.push_back(req->result_promise->get_future());

      // Send request to target worker
      ASTRADB_LOG_DEBUG("Worker {}: Enqueueing batch request {} to Worker {} (keys: {})",
                       worker_id_, req_id, target_worker_id, sub_keys.size());
      all_workers_[target_worker_id]->EnqueueBatchRequest(*req);
    }
  }

  // Wait for all cross-worker responses with timeout
  // Note: This is still blocking, but we call ProcessBatchResponses periodically
  auto start_time = absl::Now();
  size_t completed = 0;

  while (completed < futures.size()) {
    // Process batch responses (this is called from ExecutorLoop, so we need to do it here)
    ProcessBatchResponses();

    // Check if any future is ready
    for (size_t i = 0; i < futures.size(); ++i) {
      if (futures[i].valid()) {
        auto status = futures[i].wait_for(absl::ToChronoMilliseconds(absl::Milliseconds(1)));
        if (status == std::future_status::ready) {
          auto result = futures[i].get();
          all_results.push_back(std::move(result));
          completed++;
        }
      }
    }

    // Check timeout
    auto elapsed = absl::Now() - start_time;
    if (elapsed > absl::Seconds(5)) {
      ASTRADB_LOG_ERROR("Worker {}: Batch request timeout after 5 seconds", worker_id_);
      // Add empty results for any remaining futures
      for (size_t i = 0; i < futures.size(); ++i) {
        if (futures[i].valid()) {
          all_results.push_back({});
        }
      }
      break;
    }

    // Small sleep to avoid busy waiting
    absl::SleepFor(absl::Milliseconds(1));
  }

  // Aggregate results based on command type
  absl::InlinedVector<std::string, 8> final_result;
  if (cmd_type == "SINTER") {
    if (!all_results.empty()) {
      // Compute intersection of all results
      final_result = absl::InlinedVector<std::string, 8>(all_results[0].begin(), all_results[0].end());
      for (size_t i = 1; i < all_results.size(); ++i) {
        absl::flat_hash_set<std::string> current_set(all_results[i].begin(), all_results[i].end());
        absl::InlinedVector<std::string, 8> temp;
        for (const auto& member : final_result) {
          if (current_set.find(member) != current_set.end()) {
            temp.push_back(member);
          }
        }
        final_result = std::move(temp);
        if (final_result.empty()) break;
      }
    }
  }
  // TODO: Add other command types (SUNION, ZUNIONSTORE, ZINTERSTORE)

  // Convert absl::InlinedVector to std::vector for return
  return std::vector<std::string>(final_result.begin(), final_result.end());
}

inline void Worker::ProcessBatchRequest(const BatchCrossWorkerRequest& req) {
  ASTRADB_LOG_DEBUG("Worker {}: Processing batch request {} from Worker {}",
                   worker_id_, req.req_id, req.source_worker_id);

  std::vector<std::string> result;

  // Execute command on this worker's database
  if (req.cmd_type == "SINTER") {
    ASTRADB_LOG_DEBUG("Worker {}: Calling SInterLocal with {} keys", worker_id_, req.keys.size());
    result = data_shard_.GetDatabase().SInterLocal(req.keys);
    ASTRADB_LOG_DEBUG("Worker {}: SInterLocal returned {} results", worker_id_, result.size());
  }
  // Add other command types as needed

  // Send response back
  ASTRADB_LOG_DEBUG("Worker {}: Creating batch response for request {}", worker_id_, req.req_id);
  BatchCrossWorkerResponse resp{req.req_id, std::move(result)};
  if (req.source_worker_id < all_workers_.size()) {
    ASTRADB_LOG_DEBUG("Worker {}: Enqueueing batch response to Worker {}", worker_id_, req.source_worker_id);
    all_workers_[req.source_worker_id]->EnqueueBatchResponse(resp);
    ASTRADB_LOG_DEBUG("Worker {}: Batch response enqueued successfully", worker_id_);
  } else {
    ASTRADB_LOG_ERROR("Worker {}: Invalid source worker ID: {}", worker_id_, req.source_worker_id);
  }
}

inline void Worker::ProcessBatchResponses() {
  BatchCrossWorkerResponse resp;
  while (batch_resp_queue_.try_dequeue(resp)) {
    ASTRADB_LOG_DEBUG("Worker {}: Received batch response for request {}", worker_id_, resp.req_id);
    std::lock_guard<std::mutex> lock(batch_mutex_);
    auto it = pending_batch_reqs_.find(resp.req_id);
    if (it != pending_batch_reqs_.end()) {
      ASTRADB_LOG_DEBUG("Worker {}: Setting promise for request {} with {} results", worker_id_, resp.req_id, resp.result.size());
      // Set the promise value with the received result
      it->second->result_promise->set_value(std::move(resp.result));
      // Remove from pending map
      pending_batch_reqs_.erase(it);
      ASTRADB_LOG_DEBUG("Worker {}: Request {} removed from pending map", worker_id_, resp.req_id);
    } else {
      ASTRADB_LOG_ERROR("Worker {}: Received response for unknown request {}", worker_id_, resp.req_id);
    }
  }
}

}  // namespace astra::server