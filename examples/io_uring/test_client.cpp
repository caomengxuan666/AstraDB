// ==============================================================================
// Test Client for io_uring Server Examples
// ==============================================================================
// This is a simple test client to verify both raw io_uring and ASIO io_uring servers.
// It sends a message and expects a response.
// ==============================================================================

#include <asio.hpp>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#define DEFAULT_PORT 8765
#define TEST_MESSAGE "PING\r\n"

class TestClient {
 public:
  TestClient(const std::string& host, int port) : host_(host), port_(port) {}

  bool TestConnection(int num_tests = 1, int delay_ms = 0) {
    try {
      asio::io_context io_context;
      asio::ip::tcp::socket socket(io_context);
      asio::ip::tcp::endpoint endpoint(asio::ip::make_address(host_), port_);

      std::cout << "Connecting to " << host_ << ":" << port_ << "..." << std::endl;

      // Connect with timeout
      socket.connect(endpoint);
      std::cout << "Connected successfully!" << std::endl;

      // Run tests
      for (int i = 0; i < num_tests; ++i) {
        if (delay_ms > 0 && i > 0) {
          std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }

        std::cout << "\nTest " << (i + 1) << "/" << num_tests << ":" << std::endl;

        // Send message
        std::cout << "Sending: " << TEST_MESSAGE;
        asio::write(socket, asio::buffer(TEST_MESSAGE, std::strlen(TEST_MESSAGE)));

        // Receive response
        std::array<char, 1024> response;
        std::size_t bytes_read = socket.read_some(asio::buffer(response));

        std::string response_str(response.data(), bytes_read);
        std::cout << "Received: " << response_str;

        if (response_str == "+OK\r\n") {
          std::cout << "✓ Response correct!" << std::endl;
        } else {
          std::cout << "✗ Response incorrect! Expected '+OK\\r\\n'" << std::endl;
          socket.close();
          return false;
        }
      }

      socket.close();
      std::cout << "\n✓ All tests passed!" << std::endl;
      return true;

    } catch (const std::exception& e) {
      std::cerr << "✗ Connection failed: " << e.what() << std::endl;
      return false;
    }
  }

  bool TestMultipleConnections(int num_connections = 10) {
    std::cout << "\nTesting multiple connections (" << num_connections << ")..."
              << std::endl;

    int success_count = 0;

    for (int i = 0; i < num_connections; ++i) {
      std::cout << "Connection " << (i + 1) << "/" << num_connections << "...";

      try {
        asio::io_context io_context;
        asio::ip::tcp::socket socket(io_context);
        asio::ip::tcp::endpoint endpoint(asio::ip::make_address(host_), port_);

        socket.connect(endpoint);

        // Send message
        asio::write(socket, asio::buffer(TEST_MESSAGE));

        // Receive response
        std::array<char, 1024> response;
        std::size_t bytes_read = socket.read_some(asio::buffer(response));

        std::string response_str(response.data(), bytes_read);
        if (response_str == "+OK\r\n") {
          success_count++;
          std::cout << " ✓" << std::endl;
        } else {
          std::cout << " ✗ (wrong response)" << std::endl;
        }

        socket.close();
      } catch (const std::exception& e) {
        std::cout << " ✗ (" << e.what() << ")" << std::endl;
      }

      // Small delay between connections
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nResult: " << success_count << "/" << num_connections
              << " connections successful" << std::endl;

    return success_count == num_connections;
  }

 private:
  std::string host_;
  int port_;
};

void PrintUsage(const char* program_name) {
  std::cout << "Usage: " << program_name << " [options]" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -h, --host <host>     Server host (default: 127.0.0.1)"
            << std::endl;
  std::cout << "  -p, --port <port>     Server port (default: 8765)"
            << std::endl;
  std::cout << "  -n, --num <count>     Number of tests (default: 1)"
            << std::endl;
  std::cout << "  -d, --delay <ms>      Delay between tests in ms (default: 0)"
            << std::endl;
  std::cout << "  -m, --multi <count>   Test multiple connections (default: off)"
            << std::endl;
  std::cout << "  --help                 Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
  std::string host = "127.0.0.1";
  int port = DEFAULT_PORT;
  int num_tests = 1;
  int delay_ms = 0;
  int multi_connections = 0;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-h" || arg == "--host") {
      if (i + 1 < argc) {
        host = argv[++i];
      }
    } else if (arg == "-p" || arg == "--port") {
      if (i + 1 < argc) {
        port = std::atoi(argv[++i]);
      }
    } else if (arg == "-n" || arg == "--num") {
      if (i + 1 < argc) {
        num_tests = std::atoi(argv[++i]);
      }
    } else if (arg == "-d" || arg == "--delay") {
      if (i + 1 < argc) {
        delay_ms = std::atoi(argv[++i]);
      }
    } else if (arg == "-m" || arg == "--multi") {
      if (i + 1 < argc) {
        multi_connections = std::atoi(argv[++i]);
      }
    } else if (arg == "--help") {
      PrintUsage(argv[0]);
      return 0;
    }
  }

  std::cout << "========================================" << std::endl;
  std::cout << "io_uring Server Test Client" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Configuration:" << std::endl;
  std::cout << "  Host: " << host << std::endl;
  std::cout << "  Port: " << port << std::endl;
  std::cout << "========================================" << std::endl;

  TestClient client(host, port);

  bool success = true;

  if (multi_connections > 0) {
    success = client.TestMultipleConnections(multi_connections);
  } else {
    success = client.TestConnection(num_tests, delay_ms);
  }

  return success ? 0 : 1;
}