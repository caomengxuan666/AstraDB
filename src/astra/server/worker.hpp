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

#include "astra/base/concurrentqueue_wrapper.hpp"
#include "astra/base/logging.hpp"
#include "astra/commands/command_auto_register.hpp"
#include "astra/commands/command_handler.hpp"
#include "astra/commands/database.hpp"
#include "astra/protocol/resp/resp_parser.hpp"
#include "astra/protocol/resp/resp_builder.hpp"
#include "astra/protocol/resp/resp_types.hpp"

namespace astra::server {

// Forward declaration
class Worker;

// Simple CommandContext implementation for Worker
class WorkerCommandContext : public astra::commands::CommandContext {
 public:
  explicit WorkerCommandContext(astra::commands::Database* db) : db_(db) {}

  astra::commands::Database* GetDatabase() const override { return db_; }
  int GetDBIndex() const override { return 0; }
  void SetDBIndex(int index) override { (void)index; }
  bool IsAuthenticated() const override { return true; }
  void SetAuthenticated(bool auth) override { (void)auth; }

 private:
  astra::commands::Database* db_;
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
        running_(false),
        all_workers_(std::move(all_workers)) {
    
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

    void Start() {
      ASTRADB_LOG_DEBUG("Worker {}: Connection {} starting", worker_id_, conn_id_);
      DoRead();
    }

    void Send(const std::string& response) {
      ASTRADB_LOG_DEBUG("Worker {}: Connection {} sending response: {} (len={})",
                       worker_id_, conn_id_, response, response.size());

      auto self = shared_from_this();
      // Copy response to ensure it's alive during async operation
      std::string response_copy = response;
      asio::async_write(
          socket_, asio::buffer(response_copy),
          [this, self, response_copy](asio::error_code ec, size_t bytes_written) {
            if (!ec) {
              ASTRADB_LOG_DEBUG("Worker {}: Connection {} response sent (bytes={})",
                               worker_id_, conn_id_, bytes_written);
            } else {
              ASTRADB_LOG_ERROR("Worker {}: Connection {} write error: {}",
                                worker_id_, conn_id_, ec.message());
            }
          });
    }

   private:
    void DoRead() {
      auto self = shared_from_this();
      socket_.async_read_some(
          asio::buffer(buffer_),
          [this, self](asio::error_code ec, size_t bytes_transferred) {
            if (!ec) {
              ASTRADB_LOG_DEBUG("Worker {}: Connection {} received {} bytes",
                               worker_id_, conn_id_, bytes_transferred);

              // Append to receive buffer
              receive_buffer_.append(buffer_.data(), bytes_transferred);

              // Process commands (minimal parsing only)
              ProcessCommands();

              // Continue reading
              DoRead();
            } else {
              ASTRADB_LOG_ERROR("Worker {}: Connection {} read error: {}",
                                worker_id_, conn_id_, ec.message());
            }
          });
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
        CommandWithConnId cmd{conn_id_, *command_opt};
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
        ASTRADB_LOG_DEBUG("Worker {}: Executor processing command: {} for conn {}",
                         worker_id_, cmd.command.name, cmd.conn_id);

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
          std::string response = data_shard_.Execute(cmd.command);
          ResponseWithConnId resp{cmd.conn_id, response};
          resp_queue_.enqueue(resp);
        } else {
          // Forward to target worker
          ASTRADB_LOG_DEBUG("Worker {}: Forwarding command to Worker {}",
                           worker_id_, target_worker);
          CrossWorkerRequest cross_req{
            worker_id_,  // source worker
            cmd.conn_id,
            cmd.command
          };
          all_workers_[target_worker]->ProcessCrossWorkerRequest(cross_req);
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
          it->second->Send(resp.response);
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

  // For cross-worker requests, use async to avoid blocking ExecutorLoop
  auto future = std::async(std::launch::async, [this, cmd_type, worker_keys, args]() {
    std::vector<std::vector<std::string>> all_results;
    std::vector<std::future<std::vector<std::string>>> futures;

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
        all_workers_[target_worker_id]->EnqueueBatchRequest(*req);
      }
    }

    // Wait for all cross-worker responses
    for (auto& future : futures) {
      auto status = future.wait_for(std::chrono::seconds(5));
      if (status == std::future_status::ready) {
        all_results.push_back(future.get());
      } else {
        all_results.push_back({});  // Empty result on timeout
      }
    }

    // Aggregate results based on command type
    if (cmd_type == "SINTER") {
      if (all_results.empty()) {
        return std::vector<std::string>{};
      }

      // Start with first result
      std::vector<std::string> result = all_results[0];

      // Intersect with remaining results
      for (size_t i = 1; i < all_results.size(); ++i) {
        absl::flat_hash_set<std::string> current_set(all_results[i].begin(), all_results[i].end());
        std::vector<std::string> temp;
        for (const auto& member : result) {
          if (current_set.find(member) != current_set.end()) {
            temp.push_back(member);
          }
        }
        result = std::move(temp);
        if (result.empty()) break;
      }

      return result;
    }

    // For other commands, just concatenate results
    std::vector<std::string> concatenated;
    for (auto& result : all_results) {
      concatenated.insert(concatenated.end(), result.begin(), result.end());
    }
    return concatenated;
  });

  // Wait for the async task to complete (with timeout)
  auto status = future.wait_for(std::chrono::seconds(10));
  if (status == std::future_status::ready) {
    return future.get();
  } else {
    ASTRADB_LOG_ERROR("Worker {}: Batch request async task timeout", worker_id_);
    return {};
  }
}

inline void Worker::ProcessBatchRequest(const BatchCrossWorkerRequest& req) {
  ASTRADB_LOG_DEBUG("Worker {}: Processing batch request {} from Worker {}",
                   worker_id_, req.req_id, req.source_worker_id);

  std::vector<std::string> result;

  // Execute command on this worker's database
  if (req.cmd_type == "SINTER") {
    result = data_shard_.GetDatabase().SInterLocal(req.keys);
  }
  // Add other command types as needed

  // Send response back
  BatchCrossWorkerResponse resp{req.req_id, std::move(result)};
  if (req.source_worker_id < all_workers_.size()) {
    all_workers_[req.source_worker_id]->EnqueueBatchResponse(resp);
  }
}

inline void Worker::ProcessBatchResponses() {
  BatchCrossWorkerResponse resp;
  while (batch_resp_queue_.try_dequeue(resp)) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    auto it = pending_batch_reqs_.find(resp.req_id);
    if (it != pending_batch_reqs_.end()) {
      // Set the promise value
      it->second->result_promise->set_value(std::move(resp.result));
      // Remove from pending map
      pending_batch_reqs_.erase(it);
    }
  }
}

}  // namespace astra::server