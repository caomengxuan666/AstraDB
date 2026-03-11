// ==============================================================================
// ASIO io_uring Server with STRAND
// ==============================================================================
// This tests if strand is the issue with io_uring
// ==============================================================================

#define ASIO_HAS_IO_URING 1
#define ASIO_DISABLE_EPOLL 1

#include <asio.hpp>
#include <iostream>
#include <memory>

#define BUFFER_SIZE 4096
#define SERVER_PORT 8771

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::ip::tcp::socket socket, asio::any_io_executor executor)
      : socket_(std::move(socket)),
        strand_(asio::make_strand(executor)) {
    std::cout << "Session created, socket fd: " << socket_.native_handle() << std::endl;
  }

  void Start() {
    std::cout << "Session starting read with strand" << std::endl;
    DoRead();
  }

 private:
  void DoRead() {
    auto self = shared_from_this();
    std::cout << "Session calling async_read_some with strand" << std::endl;
    
    socket_.async_read_some(
        asio::buffer(data_, BUFFER_SIZE),
        asio::bind_executor(strand_, [this, self](std::error_code ec, std::size_t length) {
          std::cout << "Session read callback (with strand): ec=" << ec.message() << ", length=" << length << std::endl;
          
          if (!ec) {
            std::cout << "Received " << length << " bytes: ";
            std::cout.write(data_, length);
            std::cout << std::endl;

            // Echo back
            DoWrite(length);
          } else {
            std::cout << "Read error: " << ec.message() << std::endl;
          }
        }));
    
    std::cout << "Session async_read_some called" << std::endl;
  }

  void DoWrite(std::size_t length) {
    auto self = shared_from_this();
    
    asio::async_write(
        socket_, asio::buffer(data_, length),
        asio::bind_executor(strand_, [this, self](std::error_code ec, std::size_t /*length*/) {
          std::cout << "Session write callback (with strand): ec=" << ec.message() << std::endl;
          
          if (!ec) {
            DoRead();
          } else {
            std::cout << "Write error: " << ec.message() << std::endl;
          }
        }));
  }

  asio::ip::tcp::socket socket_;
  asio::strand<asio::any_io_executor> strand_;
  char data_[BUFFER_SIZE];
};

class Server {
 public:
  Server(asio::io_context& io_context, unsigned short port)
      : acceptor_(io_context,
                 asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
    std::cout << "Server listening on port " << port << std::endl;
    DoAccept();
  }

 private:
  void DoAccept() {
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
          std::cout << "Accept callback: ec=" << ec.message() << std::endl;
          
          if (!ec) {
            std::cout << "New connection accepted, fd: " << socket.native_handle() << std::endl;
            std::make_shared<Session>(std::move(socket), acceptor_.get_executor())->Start();
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
  std::cout << "ASIO io_uring Server with STRAND" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Port: " << port << std::endl;

  asio::io_context io_context;
  Server server(io_context, port);

  std::cout << "Press Ctrl+C to stop" << std::endl;
  io_context.run();

  return 0;
}
