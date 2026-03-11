// ==============================================================================
// ASIO + io_uring Server Example
// ==============================================================================
// This is a minimal TCP server using ASIO with io_uring backend.
// This is the same model as our main server, but simplified for debugging.
// ==============================================================================

// CRITICAL: Force ASIO to use io_uring instead of epoll on Linux
#define ASIO_HAS_IO_URING 1
#define ASIO_DISABLE_EPOLL 1

#include <asio.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

#define BUFFER_SIZE 4096
#define SERVER_PORT 8766

// Client session to track state
class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::ip::tcp::socket socket)
      : socket_(std::move(socket)),
        read_buffer_(BUFFER_SIZE),
        read_bytes_(0),
        reading_done_(false),
        response_sent_(0) {}

  void Start() {
    DoRead();
  }

 private:
  void DoRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        asio::buffer(read_buffer_),
        [this, self](std::error_code ec, std::size_t bytes_transferred) {
          if (!ec) {
            read_bytes_ = bytes_transferred;
            std::string data(read_buffer_.data(), bytes_transferred);
            std::cout << "ASIO io_uring received: " << data << std::endl;

            // Simple echo response
            response_ = "+OK\r\n";
            reading_done_ = true;
            response_sent_ = 0;

            DoWrite();
          } else {
            std::cerr << "Read error: " << ec.message() << std::endl;
          }
        });
  }

  void DoWrite() {
    auto self = shared_from_this();
    size_t bytes_to_send = response_.size() - response_sent_;

    asio::async_write(
        socket_,
        asio::buffer(response_.data() + response_sent_, bytes_to_send),
        [this, self](std::error_code ec, std::size_t bytes_transferred) {
          if (!ec) {
            response_sent_ += bytes_transferred;

            if (response_sent_ >= response_.size()) {
              // Full response sent, read again
              reading_done_ = false;
              response_.clear();
              response_sent_ = 0;
              read_bytes_ = 0;

              DoRead();
            } else {
              // Continue writing
              DoWrite();
            }
          } else {
            std::cerr << "Write error: " << ec.message() << std::endl;
          }
        });
  }

  asio::ip::tcp::socket socket_;
  std::vector<char> read_buffer_;
  std::size_t read_bytes_;
  bool reading_done_;
  std::string response_;
  std::size_t response_sent_;
};

class AsioIoUringServer {
 public:
  AsioIoUringServer(int port, std::size_t num_threads)
      : port_(port),
        num_threads_(num_threads),
        acceptor_(io_context_),
        signals_(io_context_) {}

  bool Initialize() {
    // Setup signal handling
    signals_.add(SIGINT);
    signals_.add(SIGTERM);
    signals_.async_wait([this](std::error_code, int) {
      std::cout << "Stopping server..." << std::endl;
      Stop();
    });

    // Open acceptor
    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port_);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);

    try {
      acceptor_.listen();
    } catch (const std::exception& e) {
      std::cerr << "Failed to listen on port " << port_ << ": " << e.what()
                << std::endl;
      return false;
    }

    std::cout << "ASIO io_uring server listening on port " << port_
              << std::endl;
#if defined(ASIO_HAS_IO_URING)
    std::cout << "Backend: io_uring" << std::endl;
#else
    std::cout << "Backend: epoll" << std::endl;
#endif

    return true;
  }

  void Run() {
    running_ = true;
    DoAccept();

    // Create thread pool
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < num_threads_; ++i) {
      threads.emplace_back([this] { io_context_.run(); });
    }

    std::cout << "Started with " << num_threads_ << " thread(s)" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;

    // Wait for all threads to finish
    for (auto& thread : threads) {
      thread.join();
    }
  }

  void Stop() {
    running_ = false;
    acceptor_.close();
    io_context_.stop();
  }

 private:
  void DoAccept() {
    acceptor_.async_accept([this](std::error_code ec,
                                 asio::ip::tcp::socket socket) {
      if (!ec && running_) {
        std::cout << "Accepted new connection" << std::endl;

        // Create and start session
        auto session = std::make_shared<Session>(std::move(socket));
        session->Start();

        // Accept next connection
        DoAccept();
      } else if (ec) {
        std::cerr << "Accept error: " << ec.message() << std::endl;
      }
    });
  }

  int port_;
  std::size_t num_threads_;
  bool running_ = false;

  asio::io_context io_context_;
  asio::ip::tcp::acceptor acceptor_;
  asio::signal_set signals_;
};

int main(int argc, char* argv[]) {
  int port = SERVER_PORT;
  std::size_t num_threads = 1;

  if (argc > 1) {
    port = std::atoi(argv[1]);
  }
  if (argc > 2) {
    num_threads = std::atoi(argv[2]);
  }

  std::cout << "========================================" << std::endl;
  std::cout << "ASIO io_uring Server Example" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Configuration:" << std::endl;
  std::cout << "  Port: " << port << std::endl;
  std::cout << "  Threads: " << num_threads << std::endl;
  std::cout << "  Backend: io_uring (forced)" << std::endl;
  std::cout << "========================================" << std::endl;

  AsioIoUringServer server(port, num_threads);

  if (!server.Initialize()) {
    std::cerr << "Failed to initialize server" << std::endl;
    return 1;
  }

  server.Run();

  std::cout << "Server stopped" << std::endl;
  return 0;
}
