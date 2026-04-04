#include <atomic>
#include <chrono>
#include <core/gossip_core.hpp>
#include <iostream>
#include <map>
#include <memory>
#include <net/json_serializer.hpp>
#include <net/transport_factory.hpp>
#include <thread>

using namespace libgossip;

std::atomic<int> total_events{0};

// Helper function to convert string to hex (simplified for meet)
static std::string StringToHex(const std::string& str) {
  std::string result;
  for (char c : str) {
    char hex[3];
    snprintf(hex, sizeof(hex), "%02x", static_cast<unsigned char>(c));
    result += hex;
  }
  return result;
}

// Helper function to parse hex string to node_id_t
static bool ParseNodeIdFromHex(const std::string& hex, node_id_t& node_id) {
  if (hex.size() != 32) {
    return false;
  }
  for (size_t i = 0; i < 16; ++i) {
    std::string byte_str = hex.substr(i * 2, 2);
    unsigned int byte_val;
    if (sscanf(byte_str.c_str(), "%2x", &byte_val) != 1) {
      return false;
    }
    node_id[i] = static_cast<uint8_t>(byte_val);
  }
  return true;
}

// Helper function to convert node_id_t to hex string
static std::string NodeIdToString(const node_id_t& node_id) {
  constexpr char hex_chars[] = "0123456789abcdef";
  std::string result;
  result.reserve(32);
  for (uint8_t byte : node_id) {
    result.push_back(hex_chars[byte >> 4]);
    result.push_back(hex_chars[byte & 0x0f]);
  }
  return result;
}

void print_metadata(const std::string& prefix,
                    const std::map<std::string, std::string>& metadata) {
  std::cout << prefix << " metadata:";
  if (metadata.empty()) {
    std::cout << " (empty)";
  } else {
    for (const auto& [k, v] : metadata) {
      std::cout << " " << k << "=" << v;
    }
  }
  std::cout << std::endl;
}

int main() {
  std::cout << "=== Testing 3-node metadata synchronization ===" << std::endl;

  // Create node 1
  std::cout << "\n=== Creating Node 1 ===" << std::endl;
  node_view self1;
  ParseNodeIdFromHex(StringToHex("node1").substr(0, 32), self1.id);
  self1.ip = "127.0.0.1";
  self1.port = 18001;
  self1.status = node_status::online;
  self1.heartbeat = 1;
  self1.version = 1;
  self1.config_epoch = 1;

  auto transport1 = gossip::net::transport_factory::create_transport(
      gossip::net::transport_type::tcp, "127.0.0.1", 18001);
  auto transport1_shared =
      std::shared_ptr<gossip::net::transport>(transport1.release());

  auto core1 = std::make_shared<gossip_core>(
      self1,
      [transport1_shared](const gossip_message& msg, const node_view& target) {
        transport1_shared->send_message(msg, target);
      },
      [](const node_view& node, node_status old_status) {
        total_events++;
        std::cout << "[node1] EVENT: " << NodeIdToString(node.id) << " status "
                  << static_cast<int>(old_status) << " -> "
                  << static_cast<int>(node.status)
                  << " heartbeat=" << node.heartbeat
                  << " config_epoch=" << node.config_epoch << std::endl;
        print_metadata("  [node1] " + NodeIdToString(node.id), node.metadata);
      });

  transport1_shared->set_gossip_core(core1);
  transport1_shared->set_serializer(
      std::make_unique<gossip::net::json_serializer>());
  transport1_shared->start();
  std::cout << "[node1] Started on port 18001" << std::endl;

  // Create node 2
  std::cout << "\n=== Creating Node 2 ===" << std::endl;
  node_view self2;
  ParseNodeIdFromHex(StringToHex("node2").substr(0, 32), self2.id);
  self2.ip = "127.0.0.1";
  self2.port = 18002;
  self2.status = node_status::online;
  self2.heartbeat = 1;
  self2.version = 1;
  self2.config_epoch = 1;

  auto transport2 = gossip::net::transport_factory::create_transport(
      gossip::net::transport_type::tcp, "127.0.0.1", 18002);
  auto transport2_shared =
      std::shared_ptr<gossip::net::transport>(transport2.release());

  auto core2 = std::make_shared<gossip_core>(
      self2,
      [transport2_shared](const gossip_message& msg, const node_view& target) {
        transport2_shared->send_message(msg, target);
      },
      [](const node_view& node, node_status old_status) {
        total_events++;
        std::cout << "[node2] EVENT: " << NodeIdToString(node.id) << " status "
                  << static_cast<int>(old_status) << " -> "
                  << static_cast<int>(node.status)
                  << " heartbeat=" << node.heartbeat
                  << " config_epoch=" << node.config_epoch << std::endl;
        print_metadata("  [node2] " + NodeIdToString(node.id), node.metadata);
      });

  transport2_shared->set_gossip_core(core2);
  transport2_shared->set_serializer(
      std::make_unique<gossip::net::json_serializer>());
  transport2_shared->start();
  std::cout << "[node2] Started on port 18002" << std::endl;

  // Create node 3
  std::cout << "\n=== Creating Node 3 ===" << std::endl;
  node_view self3;
  ParseNodeIdFromHex(StringToHex("node3").substr(0, 32), self3.id);
  self3.ip = "127.0.0.1";
  self3.port = 18003;
  self3.status = node_status::online;
  self3.heartbeat = 1;
  self3.version = 1;
  self3.config_epoch = 1;

  auto transport3 = gossip::net::transport_factory::create_transport(
      gossip::net::transport_type::tcp, "127.0.0.1", 18003);
  auto transport3_shared =
      std::shared_ptr<gossip::net::transport>(transport3.release());

  auto core3 = std::make_shared<gossip_core>(
      self3,
      [transport3_shared](const gossip_message& msg, const node_view& target) {
        transport3_shared->send_message(msg, target);
      },
      [](const node_view& node, node_status old_status) {
        total_events++;
        std::cout << "[node3] EVENT: " << NodeIdToString(node.id) << " status "
                  << static_cast<int>(old_status) << " -> "
                  << static_cast<int>(node.status)
                  << " heartbeat=" << node.heartbeat
                  << " config_epoch=" << node.config_epoch << std::endl;
        print_metadata("  [node3] " + NodeIdToString(node.id), node.metadata);
      });

  transport3_shared->set_gossip_core(core3);
  transport3_shared->set_serializer(
      std::make_unique<gossip::net::json_serializer>());
  transport3_shared->start();
  std::cout << "[node3] Started on port 18003" << std::endl;

  // Start tick threads
  std::atomic<bool> stopped{false};
  std::thread tick1([&]() {
    while (!stopped) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      core1->tick();
    }
  });
  std::thread tick2([&]() {
    while (!stopped) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      core2->tick();
    }
  });
  std::thread tick3([&]() {
    while (!stopped) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      core3->tick();
    }
  });

  // Wait for nodes to start
  std::cout << "\n=== Waiting for nodes to start ===" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Node 1 meets node 2 and node 3
  std::cout << "\n=== Node 1 meeting node 2 and node 3 ===" << std::endl;

  // Create node_view for meet node 2
  node_view target2;
  std::string target2_id_str = "meet_127.0.0.1_18002";
  ParseNodeIdFromHex(StringToHex(target2_id_str).substr(0, 32), target2.id);
  target2.ip = "127.0.0.1";
  target2.port = 18002;
  target2.status = node_status::unknown;
  target2.heartbeat = 0;
  target2.version = 0;
  core1->meet(target2);
  std::cout << "[node1] Meeting 127.0.0.1:18002" << std::endl;

  // Create node_view for meet node 3
  node_view target3;
  std::string target3_id_str = "meet_127.0.0.1_18003";
  ParseNodeIdFromHex(StringToHex(target3_id_str).substr(0, 32), target3.id);
  target3.ip = "127.0.0.1";
  target3.port = 18003;
  target3.status = node_status::unknown;
  target3.heartbeat = 0;
  target3.version = 0;
  core1->meet(target3);
  std::cout << "[node1] Meeting 127.0.0.1:18003" << std::endl;

  std::cout << "Waiting for nodes to discover each other..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(5));

  // Record initial event count
  int initial_events = total_events.load();
  std::cout << "\n=== Initial events: " << initial_events
            << " ===" << std::endl;

  // Node 1 updates metadata
  std::cout << "\n=== Node 1 updating metadata with slots='4-7' ==="
            << std::endl;
  std::map<std::string, std::string> metadata;
  metadata["slots"] = "4-7";
  core1->update_self_metadata(metadata);
  std::cout << "[node1] Updated metadata: slots=4-7" << std::endl;

  // Force tick
  core1->tick();
  std::cout << "[node1] Called tick() to propagate metadata" << std::endl;

  // Wait for propagation
  std::cout << "Waiting for metadata propagation..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(5));

  // Check results
  int final_events = total_events.load();
  int new_events = final_events - initial_events;

  std::cout << "\n=== Results ===" << std::endl;
  std::cout << "Events before metadata update: " << initial_events << std::endl;
  std::cout << "Events after metadata update: " << final_events << std::endl;
  std::cout << "New events (should be 2, one for node2 and one for node3): "
            << new_events << std::endl;

  if (new_events >= 2) {
    std::cout << "✅ SUCCESS: Metadata was propagated to other nodes!"
              << std::endl;
  } else {
    std::cout << "❌ FAILURE: Metadata was NOT propagated to other nodes!"
              << std::endl;
  }

  // Stop
  stopped = true;
  tick1.join();
  tick2.join();
  tick3.join();

  std::cout << "\n=== Stopping transports ===" << std::endl;

  // Stop transports first
  transport1_shared->stop();
  transport2_shared->stop();
  transport3_shared->stop();

  // Give ASIO time to clean up async operations
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Reset gossip cores to break any remaining references
  core1.reset();
  core2.reset();
  core3.reset();

  // Reset transports
  transport1_shared.reset();
  transport2_shared.reset();
  transport3_shared.reset();

  std::cout << "\n=== Test completed ===" << std::endl;

  return 0;
}
