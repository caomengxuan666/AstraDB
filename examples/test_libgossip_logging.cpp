#include <chrono>
#include <core/gossip_core.hpp>
#include <core/logger.hpp>
#include <iostream>
#include <thread>

using namespace libgossip;

int main() {
  std::cout << "=== Testing libgossip logging system ===" << std::endl;

  // Initialize libgossip logger
  Logger::Instance().Init("logs/test_libgossip.log", LogLevel::DEBUG);
  std::cout << "Logger initialized" << std::endl;

  // Test logging
  LIBGOSSIP_LOG_DEBUG("This is a DEBUG message");
  std::cout << "Called LIBGOSSIP_LOG_DEBUG" << std::endl;

  LIBGOSSIP_LOG_INFO("This is an INFO message");
  std::cout << "Called LIBGOSSIP_LOG_INFO" << std::endl;

  // Create a simple node
  node_id_t id1 = {0x01};
  node_view node1;
  node1.id = id1;
  node1.ip = "127.0.0.1";
  node1.port = 8001;
  node1.heartbeat = 1;
  node1.config_epoch = 1;

  // Add metadata
  node1.metadata["slots"] = "0-1000";
  node1.metadata["test"] = "value";

  LIBGOSSIP_LOG_DEBUG(
      "Created node1 with metadata: slots=" << node1.metadata["slots"]);
  std::cout << "Created node1 with slots metadata" << std::endl;

  // Test another node with different metadata
  node_id_t id2 = {0x02};
  node_view node2;
  node2.id = id2;
  node2.ip = "127.0.0.1";
  node2.port = 8002;
  node2.heartbeat = 1;
  node2.config_epoch = 1;

  node2.metadata["slots"] = "1001-2000";
  node2.metadata["test"] = "value";

  LIBGOSSIP_LOG_DEBUG(
      "Created node2 with metadata: slots=" << node2.metadata["slots"]);
  std::cout << "Created node2 with different slots metadata" << std::endl;

  // Test can_replace
  bool can_replace = node2.can_replace(node1);
  LIBGOSSIP_LOG_DEBUG("node2.can_replace(node1) = " << can_replace);
  std::cout << "node2.can_replace(node1) = " << can_replace << std::endl;

  // Test with same heartbeat and config epoch but different metadata
  node1.metadata["slots"] = "4-7";
  LIBGOSSIP_LOG_DEBUG("Updated node1 slots to: " << node1.metadata["slots"]);
  std::cout << "Updated node1 slots to 4-7" << std::endl;

  can_replace = node1.can_replace(node2);
  LIBGOSSIP_LOG_DEBUG(
      "node1.can_replace(node2) after metadata change = " << can_replace);
  std::cout << "node1.can_replace(node2) after metadata change = "
            << can_replace << std::endl;

  std::cout << "=== Test completed ===" << std::endl;
  std::cout << "Check logs/test_libgossip.log for libgossip output"
            << std::endl;

  return 0;
}