// ==============================================================================
// Raw TCP Test Client - No automatic line endings
// ==============================================================================
// Simple TCP client that sends exact bytes without modification
// ==============================================================================

#include <asio.hpp>
#include <iostream>
#include <cstring>

int main(int argc, char* argv[]) {
  int port = 8772;
  if (argc > 1) {
    port = std::atoi(argv[1]);
  }

  std::cout << "Connecting to 127.0.0.1:" << port << std::endl;

  try {
    asio::io_context io_context;
    asio::ip::tcp::socket socket(io_context);
    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port);

    socket.connect(endpoint);
    std::cout << "Connected!" << std::endl;

    // Send exactly "PING" without any extra characters
    const char* data = "PING";
    std::size_t length = 4;
    
    std::cout << "Sending: '" << data << "' (" << length << " bytes)" << std::endl;
    std::cout << "Hex: ";
    for (std::size_t i = 0; i < length; ++i) {
      std::cout << std::hex << std::setfill('0') << std::setw(2) 
                  << (static_cast<int>(data[i]) & 0xFF) << " ";
    }
    std::cout << std::dec << std::endl;

    asio::write(socket, asio::buffer(data, length));

    // Receive response
    char response[256];
    std::size_t bytes_read = socket.read_some(asio::buffer(response, 256));
    
    std::cout << "\nReceived " << bytes_read << " bytes:" << std::endl;
    std::cout << "Text: '";
    for (std::size_t i = 0; i < bytes_read; ++i) {
      if (response[i] >= 32 && response[i] < 127) {
        std::cout << response[i];
      } else {
        std::cout << "\\x" << std::hex << std::setfill('0') << std::setw(2) 
                    << (static_cast<int>(response[i]) & 0xFF) << std::dec;
      }
    }
    std::cout << "'" << std::endl;
    
    std::cout << "Hex: ";
    for (std::size_t i = 0; i < bytes_read; ++i) {
      std::cout << std::hex << std::setfill('0') << std::setw(2) 
                  << (static_cast<int>(response[i]) & 0xFF) << " ";
    }
    std::cout << std::dec << std::endl;
    
    std::cout << "Dec: ";
    for (std::size_t i = 0; i < bytes_read; ++i) {
      std::cout << std::dec << std::setw(3) 
                  << static_cast<int>(response[i]) << " ";
    }
    std::cout << std::endl;

  } catch (std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}