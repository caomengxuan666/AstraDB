// ==============================================================================
// Hex Dump Test - Visualize exact bytes
// ==============================================================================
// Create servers with detailed hex dump to see exact bytes received/sent
// ==============================================================================

#define ASIO_HAS_IO_URING 1
#define ASIO_DISABLE_EPOLL 1

#include <asio.hpp>
#include <iostream>
#include <iomanip>
#include <vector>
#include <memory>

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::ip::tcp::socket socket, int id, const std::string& test_name)
      : socket_(std::move(socket)),
        id_(id),
        test_name_(test_name) {
    std::cout << "[" << test_name_ << "] Session " << id_ << " created, fd: " << socket_.native_handle() << std::endl;
  }

  void Start() {
    std::cout << "\n[" << test_name_ << "] === Session " << id_ << " Starting ===" << std::endl;
    DoRead();
  }

 private:
  void DoRead() {
    auto self = shared_from_this();
    
    std::cout << "[" << test_name_ << "] Session " << id_ << " Calling async_read_some" << std::endl;
    
    socket_.async_read_some(
        asio::buffer(read_buffer_),
        [this, self](std::error_code ec, std::size_t bytes_read) {
          std::cout << "[" << test_name_ << "] Session " << id_ << " Read callback:" << std::endl;
          std::cout << "  ec=" << ec.message() << std::endl;
          std::cout << "  bytes_read=" << bytes_read << std::endl;
          
          if (!ec && bytes_read > 0) {
            std::cout << "  Read buffer size=" << sizeof(read_buffer_) << std::endl;
            std::cout << "  Actual bytes: ";
            PrintHex(read_buffer_, bytes_read);
            
            // Try to parse as text
            std::cout << "  As text: '";
            for (std::size_t i = 0; i < bytes_read; ++i) {
              if (read_buffer_[i] >= 32 && read_buffer_[i] < 127) {
                std::cout << read_buffer_[i];
              } else {
                std::cout << "\\x" << std::hex << std::setfill('0') << std::setw(2) 
                            << (static_cast<int>(read_buffer_[i]) & 0xFF) << std::dec;
              }
            }
            std::cout << "'" << std::endl;
            
            // Check for null bytes
            int null_count = 0;
            for (std::size_t i = 0; i < bytes_read; ++i) {
              if (read_buffer_[i] == 0) null_count++;
            }
            if (null_count > 0) {
              std::cout << "  ⚠️  WARNING: Found " << null_count << " null byte(s)!" << std::endl;
            }
            
            // Echo back the exact bytes received
            DoWrite(bytes_read);
          } else {
            std::cout << "[" << test_name_ << "] Session " << id_ << " Read error: " << ec.message() << std::endl;
          }
        });
    
    std::cout << "[" << test_name_ << "] Session " << id_ << " async_read_some returned" << std::endl;
  }

  void DoWrite(std::size_t bytes_to_write) {
    auto self = shared_from_this();
    
    std::cout << "[" << test_name_ << "] Session " << id_ << " Calling async_write (" << bytes_to_write << " bytes)" << std::endl;
    std::cout << "  Write buffer: ";
    PrintHex(read_buffer_, bytes_to_write);
    
    asio::async_write(
        socket_, asio::buffer(read_buffer_, bytes_to_write),
        [this, self, bytes_to_write](std::error_code ec, std::size_t bytes_written) {
          std::cout << "[" << test_name_ << "] Session " << id_ << " Write callback:" << std::endl;
          std::cout << "  ec=" << ec.message() << std::endl;
          std::cout << "  bytes_to_write=" << bytes_to_write << std::endl;
          std::cout << "  bytes_written=" << bytes_written << std::endl;
          
          if (!ec) {
            if (bytes_written == bytes_to_write) {
              std::cout << "  ✓ Write complete" << std::endl;
            } else {
              std::cout << "  ⚠️  Partial write: " << bytes_written << "/" << bytes_to_write << std::endl;
            }
            DoRead();
          } else {
            std::cout << "[" << test_name_ << "] Session " << id_ << " Write error: " << ec.message() << std::endl;
          }
        });
    
    std::cout << "[" << test_name_ << "] Session " << id_ << " async_write returned" << std::endl;
  }

  void PrintHex(const char* buffer, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
      std::cout << std::hex << std::setfill('0') << std::setw(2) 
                  << (static_cast<int>(buffer[i]) & 0xFF) << " ";
    }
    std::cout << std::dec << std::endl;
  }

  asio::ip::tcp::socket socket_;
  char read_buffer_[4096];
  int id_;
  std::string test_name_;
};

class Server {
 public:
  Server(asio::io_context& io_context, unsigned short port, const std::string& test_name)
      : acceptor_(io_context,
                 asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
        test_name_(test_name) {
    std::cout << "========================================" << std::endl;
    std::cout << test_name << " Server" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================" << std::endl;
    DoAccept();
  }

 private:
  void DoAccept() {
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
          if (!ec) {
            std::cout << "\n[" << test_name_ << "] === New Connection ===" << std::endl;
            std::cout << "[" << test_name_ << "] Client fd: " << socket.native_handle() << std::endl;
            session_counter_++;
            std::make_shared<Session>(std::move(socket), session_counter_, test_name_)->Start();
          }
          DoAccept();
        });
  }

  asio::ip::tcp::acceptor acceptor_;
  std::string test_name_;
  int session_counter_ = 0;
};

int main(int argc, char* argv[]) {
  int port = 8773;
  std::string test_name = "IO_URING_HEX_TEST";
  
  if (argc > 1) {
    test_name = argv[1];
  }
  if (argc > 2) {
    port = std::atoi(argv[2]);
  }

  asio::io_context io_context;
  Server server(io_context, port, test_name);

  io_context.run();

  return 0;
}
