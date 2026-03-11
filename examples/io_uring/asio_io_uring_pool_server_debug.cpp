// ==============================================================================
// ASIO io_uring Server with IO Context Pool - DEBUG VERSION
// ==============================================================================
// This version has extensive logging to trace the exact failure point
// ==============================================================================

#define ASIO_HAS_IO_URING 1
#define ASIO_DISABLE_EPOLL 1

#include <asio.hpp>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <chrono>

#define BUFFER_SIZE 4096
#define SERVER_PORT 8769
#define NUM_IO_CONTEXTS 4

std::mutex log_mutex;
#define LOG(msg) \
  do { \
    std::lock_guard<std::mutex> lock(log_mutex); \
    std::cout << "[" << std::this_thread::get_id() << "] " << msg << std::endl; \
  } while(0)

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::ip::tcp::socket socket, int id)
      : socket_(std::move(socket)), id_(id) {
    LOG("Session " << id_ << " created, socket fd: " << socket_.native_handle());
  }

  ~Session() {
    LOG("Session " << id_ << " destroyed");
  }

  void Start() {
    LOG("Session " << id_ << " starting read");
    DoRead();
  }

 private:
  void DoRead() {
    auto self = shared_from_this();
    LOG("Session " << id_ << " calling async_read_some");
    
    socket_.async_read_some(
        asio::buffer(data_, BUFFER_SIZE),
        [this, self](std::error_code ec, std::size_t length) {
          LOG("Session " << id_ << " read callback: ec=" << ec.message() << ", length=" << length);
          
          if (!ec) {
            std::cout << "[" << std::this_thread::get_id() << "] Received " << length << " bytes: ";
            std::cout.write(data_, length);
            std::cout << std::endl;

            // Echo back
            DoWrite(length);
          } else {
            LOG("Session " << id_ << " read error: " << ec.message());
          }
        });
    
    LOG("Session " << id_ << " async_read_some called, returning");
  }

  void DoWrite(std::size_t length) {
    auto self = shared_from_this();
    LOG("Session " << id_ << " calling async_write");
    
    asio::async_write(
        socket_, asio::buffer(data_, length),
        [this, self](std::error_code ec, std::size_t /*length*/) {
          LOG("Session " << id_ << " write callback: ec=" << ec.message());
          
          if (!ec) {
            DoRead();
          } else {
            LOG("Session " << id_ << " write error: " << ec.message());
          }
        });
    
    LOG("Session " << id_ << " async_write called, returning");
  }

  asio::ip::tcp::socket socket_;
  char data_[BUFFER_SIZE];
  int id_;
};

class Server {
 public:
  Server(asio::io_context& io_context, unsigned short port, int& session_counter)
      : acceptor_(io_context),
        session_counter_(session_counter) {
    
    LOG("Server: Creating acceptor on port " << port);
    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port);
    
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(asio::socket_base::max_listen_connections);
    
    LOG("Server: Acceptor bound to " << endpoint << ", fd: " << acceptor_.native_handle());
    DoAccept();
  }

 private:
  void DoAccept() {
    LOG("Server: Calling async_accept");
    
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
          LOG("Server: Accept callback: ec=" << ec.message());
          
          if (!ec) {
            int session_id = ++session_counter_;
            LOG("Server: New connection accepted, fd: " << socket.native_handle() << ", session_id: " << session_id);
            std::make_shared<Session>(std::move(socket), session_id)->Start();
          } else {
            LOG("Server: Accept error: " << ec.message());
          }
          
          LOG("Server: Calling DoAccept again");
          DoAccept();
        });
    
    LOG("Server: async_accept called, returning");
  }

  asio::ip::tcp::acceptor acceptor_;
  int& session_counter_;
};

int main(int argc, char* argv[]) {
  int port = SERVER_PORT;
  if (argc > 1) {
    port = std::atoi(argv[1]);
  }

  std::cout << "========================================" << std::endl;
  std::cout << "ASIO io_uring Server with IO Context Pool (DEBUG)" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Port: " << port << std::endl;
  std::cout << "IO Contexts: " << NUM_IO_CONTEXTS << std::endl;
  std::cout << "========================================" << std::endl;

  // Create multiple io_context instances
  std::vector<std::unique_ptr<asio::io_context>> io_contexts;
  std::vector<std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>>> work_guards;
  std::vector<std::thread> threads;
  int session_counter = 0;

  LOG("Creating " << NUM_IO_CONTEXTS << " io_context instances");
  for (int i = 0; i < NUM_IO_CONTEXTS; ++i) {
    LOG("Creating io_context " << i);
    auto io_context = std::make_unique<asio::io_context>();
    work_guards.push_back(
        std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
            io_context->get_executor()));
    io_contexts.push_back(std::move(io_context));
    LOG("io_context " << i << " created");
  }

  // Create server on first io_context
  LOG("Creating server on io_context 0");
  Server server(*io_contexts[0], port, session_counter);
  LOG("Server created");

  // Start worker threads
  LOG("Starting " << NUM_IO_CONTEXTS << " worker threads");
  for (int i = 0; i < NUM_IO_CONTEXTS; ++i) {
    threads.emplace_back([i, &io_contexts]() {
      LOG("Worker thread " << i << " starting, calling io_context.run()");
      io_contexts[i]->run();
      LOG("Worker thread " << i << " finished");
    });
  }

  std::cout << "========================================" << std::endl;
  std::cout << "Server listening on port " << port << std::endl;
  std::cout << "Press Ctrl+C to stop" << std::endl;
  std::cout << "========================================" << std::endl;

  // Wait for all threads
  for (auto& thread : threads) {
    thread.join();
  }

  return 0;
}
