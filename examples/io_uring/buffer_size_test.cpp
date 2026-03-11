// ==============================================================================
// Buffer Size Test - Investigate buffer corruption
// ==============================================================================
// Test different buffer sizes to see if corruption is size-dependent
// ==============================================================================

#define ASIO_HAS_IO_URING 1
#define ASIO_DISABLE_EPOLL 1

#include <asio.hpp>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::ip::tcp::socket socket)
      : socket_(std::move(socket)) {
    std::cout << "Session created, fd: " << socket_.native_handle() << std::endl;
  }

  void Start() {
    std::cout << "\n=== Starting Read Test ===" << std::endl;
    DoRead();
  }

 private:
  void DoRead() {
    auto self = shared_from_this();
    
    // Test 1: Small buffer (16 bytes)
    std::cout << "Test 1: Reading into small buffer (16 bytes)" << std::endl;
    socket_.async_read_some(
        asio::buffer(small_buffer_, 16),
        [this, self](std::error_code ec, std::size_t length) {
          if (!ec) {
            std::cout << "Received " << length << " bytes in small buffer:" << std::endl;
            PrintBuffer(small_buffer_, length);
            
            // Test 2: Medium buffer (64 bytes)
            std::cout << "\nTest 2: Reading into medium buffer (64 bytes)" << std::endl;
            socket_.async_read_some(
                asio::buffer(medium_buffer_, 64),
                [this, self](std::error_code ec2, std::size_t length2) {
                  if (!ec2) {
                    std::cout << "Received " << length2 << " bytes in medium buffer:" << std::endl;
                    PrintBuffer(medium_buffer_, length2);
                    
                    // Test 3: Large buffer (256 bytes)
                    std::cout << "\nTest 3: Reading into large buffer (256 bytes)" << std::endl;
                    socket_.async_read_some(
                        asio::buffer(large_buffer_, 256),
                        [this, self](std::error_code ec3, std::size_t length3) {
                          if (!ec3) {
                            std::cout << "Received " << length3 << " bytes in large buffer:" << std::endl;
                            PrintBuffer(large_buffer_, length3);
                            
                            // Test 4: Exact size buffer (4 bytes for "PING")
                            std::cout << "\nTest 4: Reading into exact size buffer (4 bytes)" << std::endl;
                            socket_.async_read_some(
                                asio::buffer(exact_buffer_, 4),
                                [this, self](std::error_code ec4, std::size_t length4) {
                                  if (!ec4) {
                                    std::cout << "Received " << length4 << " bytes in exact buffer:" << std::endl;
                                    PrintBuffer(exact_buffer_, length4);
                                    
                                    std::cout << "\n=== All tests complete, sending response ===" << std::endl;
                                    DoWrite(length4);
                                  } else {
                                    std::cerr << "Read error (test 4): " << ec4.message() << std::endl;
                                  }
                                });
                          } else {
                            std::cerr << "Read error (test 3): " << ec3.message() << std::endl;
                          }
                        });
                  } else {
                    std::cerr << "Read error (test 2): " << ec2.message() << std::endl;
                  }
                });
          } else {
            std::cerr << "Read error (test 1): " << ec.message() << std::endl;
          }
        });
  }

  void PrintBuffer(const char* buffer, std::size_t length) {
    // Print as text
    std::cout << "  Text: '";
    for (std::size_t i = 0; i < length; ++i) {
      if (buffer[i] >= 32 && buffer[i] < 127) {
        std::cout << buffer[i];
      } else {
        std::cout << "\\x" << std::hex << std::setfill('0') << std::setw(2) 
                  << (static_cast<int>(buffer[i]) & 0xFF) << std::dec;
      }
    }
    std::cout << "'" << std::endl;
    
    // Print as hex
    std::cout << "  Hex:  ";
    for (std::size_t i = 0; i < length; ++i) {
      std::cout << std::hex << std::setfill('0') << std::setw(2) 
                  << (static_cast<int>(buffer[i]) & 0xFF) << " ";
    }
    std::cout << std::dec << std::endl;
    
    // Print as decimal
    std::cout << "  Dec: ";
    for (std::size_t i = 0; i < length; ++i) {
      std::cout << std::dec << std::setw(3) 
                  << static_cast<int>(buffer[i]) << " ";
    }
    std::cout << std::endl;
  }

  void DoWrite(std::size_t length) {
    auto self = shared_from_this();
    
    // Send +OK response
    const char* response = "+OK\r\n";
    std::size_t response_len = 5;
    
    std::cout << "\n=== Sending Response ===" << std::endl;
    std::cout << "Response text: '" << response << "' (" << response_len << " bytes)" << std::endl;
    PrintBuffer(response, response_len);
    
    asio::async_write(
        socket_, asio::buffer(response, response_len),
        [this, self](std::error_code ec, std::size_t /*length*/) {
          if (!ec) {
            std::cout << "Response sent successfully" << std::endl;
            DoRead();  // Continue reading
          } else {
            std::cerr << "Write error: " << ec.message() << std::endl;
          }
        });
  }

  asio::ip::tcp::socket socket_;
  char small_buffer_[16];
  char medium_buffer_[64];
  char large_buffer_[256];
  char exact_buffer_[4];
};

class Server {
 public:
  Server(asio::io_context& io_context, unsigned short port)
      : acceptor_(io_context,
                 asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
    std::cout << "========================================" << std::endl;
    std::cout << "Buffer Size Test Server" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Testing buffer corruption with different sizes" << std::endl;
    std::cout << "========================================" << std::endl;
    DoAccept();
  }

 private:
  void DoAccept() {
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
          if (!ec) {
            std::cout << "\n=== New Connection ===" << std::endl;
            std::cout << "Client fd: " << socket.native_handle() << std::endl;
            std::make_shared<Session>(std::move(socket))->Start();
          }
          DoAccept();
        });
  }

  asio::ip::tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
  int port = 8772;
  if (argc > 1) {
    port = std::atoi(argv[1]);
  }

  asio::io_context io_context;
  Server server(io_context, port);

  std::cout << "Server running, press Ctrl+C to stop" << std::endl;
  io_context.run();

  return 0;
}