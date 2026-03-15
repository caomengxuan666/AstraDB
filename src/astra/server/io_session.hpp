// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <array>
#include <asio.hpp>
#include <memory>
#include <string>

#include "astra/base/logging.hpp"
#include "astra/protocol/resp/resp_parser.hpp"
#include "astra/protocol/resp/resp_builder.hpp"
#include "astra/protocol/resp/resp_types.hpp"

namespace astra::server {

// Simple session for handling a single client connection
// Follows best practices: no strand needed (one io_context per thread)
class IOSession : public std::enable_shared_from_this<IOSession> {
 public:
  explicit IOSession(asio::ip::tcp::socket socket, size_t worker_id)
      : socket_(std::move(socket)),
        worker_id_(worker_id) {
    ASTRADB_LOG_INFO("Worker {}: Session created", worker_id_);
  }

  void Start() {
    ASTRADB_LOG_INFO("Worker {}: Session starting", worker_id_);
    DoRead();
  }

 private:
  void DoRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        asio::buffer(buffer_),
        [this, self](asio::error_code ec, size_t bytes_transferred) {
          if (!ec) {
            ASTRADB_LOG_INFO("Worker {}: Received {} bytes", worker_id_,
                             bytes_transferred);

            // Append to receive buffer
            receive_buffer_.append(buffer_.data(), bytes_transferred);

            // Process all complete commands in buffer (IO thread: minimal parsing only)
            ProcessCommands();

            // Continue reading
            DoRead();
          } else {
            ASTRADB_LOG_ERROR("Worker {}: Read error: {}", worker_id_,
                              ec.message());
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
        ASTRADB_LOG_ERROR("Worker {}: Failed to parse RESP command", worker_id_);
        // Send error and close
        SendError("ERR invalid RESP protocol");
        return;
      }

      // Calculate how many bytes were consumed
      size_t consumed = receive_buffer_.size() - data_view.size();
      receive_buffer_.erase(0, consumed);

      // Parse command from RESP value
      auto command_opt = astra::protocol::RespParser::ParseCommand(*value_opt);
      if (!command_opt) {
        ASTRADB_LOG_ERROR("Worker {}: Failed to parse command", worker_id_);
        SendError("ERR invalid command format");
        continue;
      }

      // Handle the command (IO thread: simple commands only)
      HandleCommand(*command_opt);
    }
  }

void HandleCommand(const astra::protocol::Command& command) {
    ASTRADB_LOG_INFO("Worker {}: Received command: {}", worker_id_,
                     command.name);

    // Simple PING command handler
    if (command.name == "PING") {
      SendSimpleString("PONG");
    } else {
      SendError("ERR unknown command");
    }
  }

  void DoWrite(const std::string& response) {
    auto self = shared_from_this();
    asio::async_write(
        socket_, asio::buffer(response),
        [this, self](asio::error_code ec, size_t /*bytes_written*/) {
          if (!ec) {
            ASTRADB_LOG_INFO("Worker {}: Response sent", worker_id_);
          } else {
            ASTRADB_LOG_ERROR("Worker {}: Write error: {}", worker_id_,
                              ec.message());
          }
        });
  }

  void SendSimpleString(std::string_view str) {
    std::string response = astra::protocol::RespBuilder::BuildSimpleString(str);
    DoWrite(response);
  }

  void SendError(std::string_view msg) {
    std::string response = astra::protocol::RespBuilder::BuildError(msg);
    DoWrite(response);
  }

  asio::ip::tcp::socket socket_;
  std::array<char, 1024> buffer_;
  std::string receive_buffer_;
  size_t worker_id_;
};

}  // namespace astra::server