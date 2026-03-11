// ==============================================================================
// Raw io_uring Server Example
// ==============================================================================
// This is a minimal TCP server using raw liburing API for debugging.
// It helps us understand how io_uring works and compare with ASIO implementation.
// ==============================================================================

#include <arpa/inet.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

#define BUFFER_SIZE 4096
#define MAX_EVENTS 256
#define SERVER_PORT 8765

// Client context to track read/write state
struct ClientContext {
  int fd;
  char read_buffer[BUFFER_SIZE];
  size_t read_bytes;
  bool has_data;
  std::string response;
  size_t response_sent;
  bool reading_done;

  ClientContext(int sock_fd) : fd(sock_fd), read_bytes(0), has_data(false), response_sent(0), reading_done(false) {
    memset(read_buffer, 0, BUFFER_SIZE);
  }
};

class IoUringServer {
 public:
  IoUringServer(int port) : port_(port), running_(false) {}

  ~IoUringServer() {
    if (ring_.ring_fd > 0) {
      io_uring_queue_exit(&ring_);
    }
    if (listen_fd_ > 0) {
      close(listen_fd_);
    }
  }

  bool Initialize() {
    // Create listening socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      std::cerr << "Failed to create socket" << std::endl;
      return false;
    }

    // Set SO_REUSEADDR
    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
      std::cerr << "Failed to set SO_REUSEADDR" << std::endl;
      close(listen_fd_);
      return false;
    }

    // Bind to address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      std::cerr << "Failed to bind to port " << port_ << std::endl;
      close(listen_fd_);
      return false;
    }

    // Listen
    if (listen(listen_fd_, 128) < 0) {
      std::cerr << "Failed to listen" << std::endl;
      close(listen_fd_);
      return false;
    }

    // Make socket non-blocking
    int flags = fcntl(listen_fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
      std::cerr << "Failed to make socket non-blocking" << std::endl;
      close(listen_fd_);
      return false;
    }

    // Initialize io_uring
    if (io_uring_queue_init(MAX_EVENTS, &ring_, 0) < 0) {
      std::cerr << "Failed to initialize io_uring" << std::endl;
      close(listen_fd_);
      return false;
    }

    std::cout << "Raw io_uring server listening on port " << port_ << std::endl;
    return true;
  }

  void StartAccept() {
    // Submit accept request
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
      std::cerr << "Failed to get SQE for accept" << std::endl;
      return;
    }

    io_uring_prep_accept(sqe, listen_fd_, (struct sockaddr*)&client_addr_,
                         &client_addr_len_, 0);
    io_uring_sqe_set_data(sqe, (void*)1);  // 1 = accept operation
    io_uring_submit(&ring_);
  }

  void Run() {
    running_ = true;
    StartAccept();

    while (running_) {
      int ret = io_uring_wait_cqe(&ring_, &cqe_);
      if (ret < 0) {
        std::cerr << "Wait CQE failed: " << ret << std::endl;
        break;
      }

      // Process all available completions
      struct io_uring_cqe* cqes[MAX_EVENTS];
      unsigned int count = io_uring_peek_batch_cqe(&ring_, cqes, MAX_EVENTS);

      for (unsigned int i = 0; i < count; i++) {
        ProcessCompletion(cqes[i]);
        io_uring_cqe_seen(&ring_, cqes[i]);
      }

      if (count == 0) {
        // Process the single CQE we got
        ProcessCompletion(cqe_);
        io_uring_cqe_seen(&ring_, cqe_);
      }
    }
  }

  void Stop() {
    running_ = false;
  }

 private:
  void ProcessCompletion(struct io_uring_cqe* cqe) {
    unsigned long long data = (unsigned long long)io_uring_cqe_get_data(cqe);
    int res = cqe->res;

    if (res < 0) {
      std::cerr << "Operation failed: " << strerror(-res) << std::endl;
      if (data > 1) {  // Not an accept operation
        int client_fd = (int)data;
        CloseClient(client_fd);
      }
      return;
    }

    switch (data) {
      case 1:  // Accept operation
        HandleAccept(res);
        break;
      default:  // Client operation (data = client_fd)
        HandleClientOperation((int)data, res);
        break;
    }
  }

  void HandleAccept(int client_fd) {
    if (client_fd < 0) {
      std::cerr << "Accept failed: " << strerror(-client_fd) << std::endl;
      // Submit new accept request
      StartAccept();
      return;
    }

    std::cout << "Accepted new client: fd=" << client_fd << std::endl;

    // Make client socket non-blocking
    int flags = fcntl(client_fd, F_GETFL, 0);
    if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      std::cerr << "Failed to make client socket non-blocking" << std::endl;
      close(client_fd);
      StartAccept();
      return;
    }

    // Create client context
    clients_[client_fd] = std::make_unique<ClientContext>(client_fd);

    // Submit read request
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
      std::cerr << "Failed to get SQE for read" << std::endl;
      CloseClient(client_fd);
      StartAccept();
      return;
    }

    auto* ctx = clients_[client_fd].get();
    io_uring_prep_read(sqe, client_fd, ctx->read_buffer, BUFFER_SIZE - 1, 0);
    io_uring_sqe_set_data(sqe, (void*)(unsigned long long)client_fd);
    io_uring_submit(&ring_);

    // Submit next accept request
    sqe = io_uring_get_sqe(&ring_);
    if (sqe) {
      io_uring_prep_accept(sqe, listen_fd_, (struct sockaddr*)&client_addr_,
                           &client_addr_len_, 0);
      io_uring_sqe_set_data(sqe, (void*)1);
      io_uring_submit(&ring_);
    }
  }

  void HandleClientOperation(int client_fd, int res) {
    if (clients_.find(client_fd) == clients_.end()) {
      std::cerr << "Unknown client fd: " << client_fd << std::endl;
      return;
    }

    auto* ctx = clients_[client_fd].get();

    if (ctx->reading_done) {
      // Write operation completed
      ctx->response_sent += res;

      if (ctx->response_sent >= ctx->response.size()) {
        // Full response sent, read again
        ctx->reading_done = false;
        ctx->read_bytes = 0;
        ctx->response_sent = 0;
        ctx->response.clear();

        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (sqe) {
          io_uring_prep_read(sqe, client_fd, ctx->read_buffer, BUFFER_SIZE - 1, 0);
          io_uring_sqe_set_data(sqe, (void*)(unsigned long long)client_fd);
          io_uring_submit(&ring_);
        }
      } else {
        // Continue writing
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (sqe) {
          io_uring_prep_write(sqe, client_fd,
                              ctx->response.c_str() + ctx->response_sent,
                              ctx->response.size() - ctx->response_sent, 0);
          io_uring_sqe_set_data(sqe, (void*)(unsigned long long)client_fd);
          io_uring_submit(&ring_);
        }
      }
    } else {
      // Read operation completed
      ctx->read_bytes = res;

      if (res <= 0) {
        std::cout << "Client fd=" << client_fd << " disconnected" << std::endl;
        CloseClient(client_fd);
        return;
      }

      ctx->read_buffer[res] = '\0';
      std::cout << "Received from fd=" << client_fd << ": " << ctx->read_buffer
                << std::endl;

      // Simple echo response
      ctx->response = std::string("+OK\r\n");
      ctx->reading_done = true;

      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
      if (sqe) {
        io_uring_prep_write(sqe, client_fd, ctx->response.c_str(),
                            ctx->response.size(), 0);
        io_uring_sqe_set_data(sqe, (void*)(unsigned long long)client_fd);
        io_uring_submit(&ring_);
      }
    }
  }

  void CloseClient(int client_fd) {
    clients_.erase(client_fd);
    close(client_fd);
  }

  int port_;
  int listen_fd_ = -1;
  struct io_uring ring_;
  struct io_uring_cqe* cqe_;
  struct sockaddr_in client_addr_;
  socklen_t client_addr_len_ = sizeof(client_addr_);
  bool running_;

  std::unordered_map<int, std::unique_ptr<ClientContext>> clients_;
};

int main(int argc, char* argv[]) {
  int port = SERVER_PORT;
  if (argc > 1) {
    port = atoi(argv[1]);
  }

  IoUringServer server(port);

  if (!server.Initialize()) {
    std::cerr << "Failed to initialize server" << std::endl;
    return 1;
  }

  std::cout << "Starting raw io_uring server on port " << port << std::endl;
  std::cout << "Press Ctrl+C to stop" << std::endl;

  server.Run();

  return 0;
}
