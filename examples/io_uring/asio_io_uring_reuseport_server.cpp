// SO_REUSEPORT Test for io_uring debugging
// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include <asio.hpp>
#include <csignal>
#include <iostream>
#include <memory>
#include <vector>

#include <sys/socket.h>

// SO_REUSEPORT option for Linux
#ifndef SO_REUSEPORT
#define SO_REUSEPORT 15
#endif

class ReusePortSession : public std::enable_shared_from_this<ReusePortSession> {
 public:
  explicit ReusePortSession(asio::ip::tcp::socket socket)
      : socket_(std::move(socket)), strand_(socket_.get_executor()) {}

  void Start() {
    std::cout << "[Worker " << std::this_thread::get_id()
              << "] Session starting, fd: " << socket_.native_handle() << std::endl;
    DoRead();
  }

 private:
  void DoRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        asio::buffer(data_),
        asio::bind_executor(strand_, [this, self](asio::error_code ec,
                                                     size_t bytes_transferred) {
          if (!ec) {
            std::cout << "[Worker " << std::this_thread::get_id()
                      << "] Received " << bytes_transferred << " bytes: "
                      << std::string(data_.data(), bytes_transferred) << std::endl;

            // Echo back
            std::string response = "+OK\r\n";
            DoWrite(response);
          } else if (ec != asio::error::eof) {
            std::cerr << "[Worker " << std::this_thread::get_id()
                      << "] Read error: " << ec.message() << std::endl;
          }
        }));
  }

  void DoWrite(const std::string& response) {
    auto self = shared_from_this();
    asio::async_write(
        socket_, asio::buffer(response),
        asio::bind_executor(strand_, [this, self, response](asio::error_code ec,
                                                             size_t /*bytes*/) {
          if (!ec) {
            std::cout << "[Worker " << std::this_thread::get_id()
                      << "] Sent response" << std::endl;
            DoRead();
          } else {
            std::cerr << "[Worker " << std::this_thread::get_id()
                      << "] Write error: " << ec.message() << std::endl;
          }
        }));
  }

  asio::ip::tcp::socket socket_;
  asio::strand<asio::ip::tcp::socket::executor_type> strand_;
  std::array<char, 1024> data_;
};

class ReusePortWorker {
 public:
  explicit ReusePortWorker(size_t worker_id, asio::io_context& io_context,
                          const std::string& host, uint16_t port)
      : worker_id_(worker_id),
        io_context_(io_context),
        acceptor_(io_context) {
    asio::ip::tcp::endpoint endpoint(asio::ip::make_address(host), port);

    // Enable address reuse
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));

    // Enable SO_REUSEPORT for kernel-level load balancing
    asio::error_code ec;
    acceptor_.set_option(
        asio::detail::socket_option::boolean<SOL_SOCKET, SO_REUSEPORT>(true),
        ec);
    if (ec) {
      std::cerr << "[Worker " << worker_id_
                << "] SO_REUSEPORT not supported: " << ec.message() << std::endl;
      std::cerr << "[Worker " << worker_id_
                << "] Falling back to single acceptor mode" << std::endl;
    } else {
      std::cout << "[Worker " << worker_id_
                << "] SO_REUSEPORT enabled for kernel load balancing" << std::endl;
    }

    acceptor_.bind(endpoint, ec);
    if (ec) {
      throw std::runtime_error("Failed to bind acceptor: " + ec.message());
    }

    acceptor_.listen(asio::socket_base::max_listen_connections);

    std::cout << "[Worker " << worker_id_ << "] Listening on " << host << ":"
              << port << std::endl;

    StartAccept();
  }

 private:
  void StartAccept() {
    acceptor_.async_accept(
        [this](asio::error_code ec, asio::ip::tcp::socket socket) {
          if (!ec) {
            std::cout << "[Worker " << worker_id_
                      << "] New connection accepted, fd: " << socket.native_handle()
                      << std::endl;
            auto session = std::make_shared<ReusePortSession>(std::move(socket));
            session->Start();
            total_connections_++;
          } else {
            std::cerr << "[Worker " << worker_id_
                      << "] Accept error: " << ec.message() << std::endl;
          }

          // Continue accepting
          StartAccept();
        });
  }

  size_t worker_id_;
  asio::io_context& io_context_;
  asio::ip::tcp::acceptor acceptor_;
  std::atomic<uint64_t> total_connections_{0};
};

int main(int argc, char* argv[]) {
  std::cout << "========================================" << std::endl;
  std::cout << "ASIO io_uring Server with SO_REUSEPORT" << std::endl;
  std::cout << "========================================" << std::endl;

  std::string host = "0.0.0.0";
  uint16_t port = 8772;
  size_t num_workers = 4;

  if (argc > 1) port = std::atoi(argv[1]);
  if (argc > 2) num_workers = std::atoi(argv[2]);

  std::cout << "Configuration:" << std::endl;
  std::cout << "  Host: " << host << std::endl;
  std::cout << "  Port: " << port << std::endl;
  std::cout << "  Workers: " << num_workers << std::endl;
  std::cout << "========================================" << std::endl;

  // Create io_context pool
  std::vector<std::unique_ptr<asio::io_context>> io_contexts;
  std::vector<std::unique_ptr<ReusePortWorker>> workers;
  std::vector<std::thread> threads;

  for (size_t i = 0; i < num_workers; ++i) {
    auto io_context = std::make_unique<asio::io_context>();
    auto worker = std::make_unique<ReusePortWorker>(i, *io_context, host, port);

    io_contexts.push_back(std::move(io_context));
    workers.push_back(std::move(worker));
  }

  // Start worker threads
  std::cout << "Starting " << num_workers << " worker threads..." << std::endl;
  for (size_t i = 0; i < num_workers; ++i) {
    threads.emplace_back([i, &io_contexts]() {
      std::cout << "[Thread " << i << "] Started" << std::endl;
      io_contexts[i]->run();
      std::cout << "[Thread " << i << "] Exited" << std::endl;
    });
  }

  std::cout << "========================================" << std::endl;
  std::cout << "Server running with SO_REUSEPORT" << std::endl;
  std::cout << "Press Ctrl+C to stop" << std::endl;
  std::cout << "========================================" << std::endl;

  // Wait for all threads
  for (auto& thread : threads) {
    thread.join();
  }

  std::cout << "Server stopped" << std::endl;
  return 0;
}
