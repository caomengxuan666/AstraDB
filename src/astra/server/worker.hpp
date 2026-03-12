// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <asio.hpp>
#include <memory>
#include <string>
#include <thread>

#include "astra/base/concurrentqueue_wrapper.hpp"
#include "astra/base/logging.hpp"
#include "astra/protocol/resp/resp_parser.hpp"
#include "astra/protocol/resp/resp_builder.hpp"
#include "astra/protocol/resp/resp_types.hpp"

namespace astra::server {

// Simple data shard (placeholder for now)
// TODO: Replace with actual database implementation
class DataShard {
 public:
  explicit DataShard(size_t shard_id) : shard_id_(shard_id) {}

  // Execute a command and return result
  // For now, just handle PING
  std::string Execute(const astra::protocol::Command& command) {
    ASTRADB_LOG_DEBUG("Shard {}: Executing command: {}", shard_id_, command.name);
    
    if (command.name == "PING") {
      auto response = astra::protocol::RespBuilder::BuildSimpleString("PONG");
      ASTRADB_LOG_DEBUG("Shard {}: PING response: {}", shard_id_, response);
      return response;
    } else {
      return astra::protocol::RespBuilder::BuildError("ERR unknown command");
    }
  }

 private:
  size_t shard_id_;
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

// Worker - NO SHARING architecture
// Each worker is a completely independent "mini server"
class Worker {
 public:
  explicit Worker(size_t worker_id, const std::string& host, uint16_t port)
      : worker_id_(worker_id),
        io_context_(),
        acceptor_(io_context_),
        data_shard_(worker_id),
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

  void ExecutorLoop() {
    while (running_) {
      CommandWithConnId cmd;
      if (cmd_queue_.try_dequeue(cmd)) {
        ASTRADB_LOG_DEBUG("Worker {}: Executor processing command: {} for conn {}",
                         worker_id_, cmd.command.name, cmd.conn_id);

        // Execute command (using this worker's data shard)
        std::string response = data_shard_.Execute(cmd.command);

        // Enqueue response to be sent by IO thread
        ResponseWithConnId resp{cmd.conn_id, response};
        resp_queue_.enqueue(resp);
      } else {
        std::this_thread::yield();
      }
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

  absl::flat_hash_map<uint64_t, std::shared_ptr<Connection>> connections_;

  asio::steady_timer response_timer_{io_context_};  // Timer for checking response queue

  std::thread io_thread_;
  std::thread exec_thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> next_conn_id_{0};
};

}  // namespace astra::server
