// ==============================================================================
// ASIO io_uring Server with IO Context Pool
// ==============================================================================
// This tests if io_uring works with multiple io_context instances
// ==============================================================================

#define ASIO_HAS_IO_URING 1
#define ASIO_DISABLE_EPOLL 1

#include <asio.hpp>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#define BUFFER_SIZE 4096
#define SERVER_PORT 8768
#define NUM_IO_CONTEXTS 4

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::ip::tcp::socket socket)
      : socket_(std::move(socket)) {}

  void Start() {
    DoRead();
  }

 private:
  void DoRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        asio::buffer(data_, BUFFER_SIZE),
        [this, self](std::error_code ec, std::size_t length) {
          if (!ec) {
            std::cout << "Received " << length << " bytes: ";
            std::cout.write(data_, length);
            std::cout << std::endl;

            // Echo back
            DoWrite(length);
          } else {
            std::cerr << "Read error: " << ec.message() << std::endl;
          }
        });
  }

  void DoWrite(std::size_t length) {
    auto self = shared_from_this();
    asio::async_write(
        socket_, asio::buffer(data_, length),
        [this, self](std::error_code ec, std::size_t /*length*/) {
          if (!ec) {
            DoRead();
          } else {
            std::cerr << "Write error: " << ec.message() << std::endl;
          }
        });
  }

  asio::ip::tcp::socket socket_;
  char data_[BUFFER_SIZE];
};

class Server {
 public:
  Server(asio::io_context& io_context, unsigned short port)
      : acceptor_(io_context,
                 asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
    DoAccept();
  }

 private:
  void DoAccept() {
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
          if (!ec) {
            std::cout << "New connection accepted" << std::endl;
            std::make_shared<Session>(std::move(socket))->Start();
          }
          DoAccept();
        });
  }

  asio::ip::tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
  int port = SERVER_PORT;
  if (argc > 1) {
    port = std::atoi(argv[1]);
  }

  std::cout << "========================================" << std::endl;
  std::cout << "ASIO io_uring Server with IO Context Pool" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Port: " << port << std::endl;
  std::cout << "IO Contexts: " << NUM_IO_CONTEXTS << std::endl;

  // Create multiple io_context instances
  std::vector<std::unique_ptr<asio::io_context>> io_contexts;
  std::vector<std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>>> work_guards;
  std::vector<std::thread> threads;

  for (int i = 0; i < NUM_IO_CONTEXTS; ++i) {
    auto io_context = std::make_unique<asio::io_context>();
    work_guards.push_back(
        std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
            io_context->get_executor()));
    io_contexts.push_back(std::move(io_context));
  }

  // Create server on first io_context
  Server server(*io_contexts[0], port);

  // Start worker threads
  std::cout << "Starting " << NUM_IO_CONTEXTS << " worker threads..." << std::endl;
  for (int i = 0; i < NUM_IO_CONTEXTS; ++i) {
    threads.emplace_back([i, &io_contexts]() {
      std::cout << "Worker thread " << i << " started" << std::endl;
      io_contexts[i]->run();
    });
  }

  std::cout << "Server listening on port " << port << std::endl;
  std::cout << "Press Ctrl+C to stop" << std::endl;

  // Wait for all threads
  for (auto& thread : threads) {
    thread.join();
  }

  return 0;
}
