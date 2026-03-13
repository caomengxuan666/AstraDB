// SO_REUSEPORT Simple Test for io_uring debugging
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
      : socket_(std::move(socket)),
        thread_id_(std::hash<std::thread::id>()(std::this_thread::get_id())) {}

  void Start() {
    printf("[%zu] Session starting, fd: %d\n", thread_id_,
           socket_.native_handle());
    DoRead();
  }

 private:
  void DoRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        asio::buffer(data_), [this, self](asio::error_code ec,
                                            size_t bytes_transferred) {
          if (!ec) {
            printf("[%zu] Received %zu bytes\n", thread_id_,
                   bytes_transferred);

            // Echo back
            std::string response = "+OK\r\n";
            DoWrite(response);
          } else if (ec != asio::error::eof) {
            printf("[%zu] Read error: %s\n", thread_id_,
                   ec.message().c_str());
          }
        });
  }

  void DoWrite(const std::string& response) {
    auto self = shared_from_this();
    asio::async_write(socket_, asio::buffer(response),
                      [this, self, response](asio::error_code ec,
                                             size_t /*bytes*/) {
                        if (!ec) {
                          printf("[%zu] Sent response\n", thread_id_);
                          DoRead();
                        } else {
                          printf("[%zu] Write error: %s\n", thread_id_,
                                 ec.message().c_str());
                        }
                      });
  }

  asio::ip::tcp::socket socket_;
  size_t thread_id_;
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
      printf("[Worker %zu] SO_REUSEPORT not supported: %s\n", worker_id_,
             ec.message().c_str());
      printf("[Worker %zu] Falling back to single acceptor mode\n",
             worker_id_);
    } else {
      printf("[Worker %zu] SO_REUSEPORT enabled for kernel load balancing\n",
             worker_id_);
    }

    acceptor_.bind(endpoint, ec);
    if (ec) {
      throw std::runtime_error("Failed to bind acceptor: " + ec.message());
    }

    acceptor_.listen(asio::socket_base::max_listen_connections);

    printf("[Worker %zu] Listening on %s:%d\n", worker_id_, host.c_str(), port);

    StartAccept();
  }

 private:
  void StartAccept() {
    acceptor_.async_accept(
        [this](asio::error_code ec, asio::ip::tcp::socket socket) {
          if (!ec) {
            printf("[Worker %zu] New connection accepted, fd: %d\n",
                   worker_id_, socket.native_handle());
            auto session =
                std::make_shared<ReusePortSession>(std::move(socket));
            session->Start();
            total_connections_++;
          } else {
            printf("[Worker %zu] Accept error: %s\n", worker_id_,
                   ec.message().c_str());
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
  printf("========================================\n");
  printf("ASIO io_uring Server with SO_REUSEPORT\n");
  printf("========================================\n");

  std::string host = "0.0.0.0";
  uint16_t port = 8772;
  size_t num_workers = 4;

  if (argc > 1) port = std::atoi(argv[1]);
  if (argc > 2) num_workers = std::atoi(argv[2]);

  printf("Configuration:\n");
  printf("  Host: %s\n", host.c_str());
  printf("  Port: %d\n", port);
  printf("  Workers: %zu\n", num_workers);
  printf("========================================\n");

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
  printf("Starting %zu worker threads...\n", num_workers);
  for (size_t i = 0; i < num_workers; ++i) {
    threads.emplace_back([i, &io_contexts]() {
      printf("[Thread %zu] Started\n", i);
      io_contexts[i]->run();
      printf("[Thread %zu] Exited\n", i);
    });
  }

  printf("========================================\n");
  printf("Server running with SO_REUSEPORT\n");
  printf("Press Ctrl+C to stop\n");
  printf("========================================\n");

  // Wait for all threads
  for (auto& thread : threads) {
    thread.join();
  }

  printf("Server stopped\n");
  return 0;
}