#include <chrono>
#include <core/gossip_core.hpp>
#include <iostream>
#include <map>
#include <memory>
#include <net/json_serializer.hpp>
#include <net/transport_factory.hpp>
#include <thread>
#include <atomic>
#include <cassert>

using namespace libgossip;

std::atomic<int> node1_events{0};
std::atomic<int> node2_events{0};
std::atomic<bool> node1_received_slot_metadata{false};
std::atomic<bool> node2_received_node1_real_id{false};

static std::string StringToHex(const std::string& str) {
  std::string result;
  for (char c : str) {
    char hex[3];
    snprintf(hex, sizeof(hex), "%02x", static_cast<unsigned char>(c));
    result += hex;
  }
  return result;
}

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
  std::cout << "=== Testing temporary ID and metadata propagation ===" << std::endl;

  node_id_t node1_real_id{};
  ParseNodeIdFromHex("00000000000000000000000000000001", node1_real_id);
  
  node_id_t node2_real_id{};
  ParseNodeIdFromHex("00000000000000000000000000000002", node2_real_id);

  std::cout << "\n=== Creating Node 1 (ID: " << NodeIdToString(node1_real_id) << ") ===" << std::endl;
  node_view self1;
  self1.id = node1_real_id;
  self1.ip = "127.0.0.1";
  self1.port = 19001;
  self1.status = node_status::online;
  self1.heartbeat = 1;
  self1.version = 1;
  self1.config_epoch = 1;
  self1.metadata["slots"] = "";
  self1.metadata["config_epoch"] = "1";

  auto transport1 = gossip::net::transport_factory::create_transport(
      gossip::net::transport_type::tcp, "127.0.0.1", 19001);
  auto transport1_shared = std::shared_ptr<gossip::net::transport>(transport1.release());

  auto core1 = std::make_shared<gossip_core>(
      self1,
      [transport1_shared](const gossip_message& msg, const node_view& target) {
        std::cout << "[node1] SENDING to " << target.ip << ":" << target.port 
                  << ", entries=" << msg.entries.size() << std::endl;
        for (size_t i = 0; i < msg.entries.size(); ++i) {
          std::cout << "[node1]   entry[" << i << "]: id=" << NodeIdToString(msg.entries[i].id);
          if (msg.entries[i].metadata.count("slots")) {
            std::cout << ", slots='" << msg.entries[i].metadata.at("slots") << "'";
          }
          std::cout << std::endl;
        }
        transport1_shared->send_message(msg, target);
      },
      [&](const node_view& node, node_status old_status) {
        node1_events++;
        std::cout << "[node1] EVENT: id=" << NodeIdToString(node.id)
                  << ", ip=" << node.ip << ":" << node.port
                  << ", status " << static_cast<int>(old_status)
                  << " -> " << static_cast<int>(node.status)
                  << ", heartbeat=" << node.heartbeat
                  << ", config_epoch=" << node.config_epoch << std::endl;
        print_metadata("  [node1] " + NodeIdToString(node.id), node.metadata);
        
        if (node.id == node2_real_id) {
          node2_received_node1_real_id = true;
          std::cout << "[node1] ✓ Received node2's REAL ID!" << std::endl;
        }
        
        if (node.metadata.count("slots") && !node.metadata.at("slots").empty()) {
          node1_received_slot_metadata = true;
          std::cout << "[node1] ✓ Received slot metadata: " << node.metadata.at("slots") << std::endl;
        }
      });

  transport1_shared->set_gossip_core(core1);
  auto serializer1 = std::make_unique<gossip::net::json_serializer>();
  transport1_shared->set_serializer(std::move(serializer1));
  
  if (transport1_shared->start() != gossip::net::error_code::success) {
    std::cerr << "Failed to start transport1" << std::endl;
    return 1;
  }

  std::cout << "\n=== Creating Node 2 (ID: " << NodeIdToString(node2_real_id) << ") ===" << std::endl;
  node_view self2;
  self2.id = node2_real_id;
  self2.ip = "127.0.0.1";
  self2.port = 19002;
  self2.status = node_status::online;
  self2.heartbeat = 1;
  self2.version = 1;
  self2.config_epoch = 1;
  self2.metadata["slots"] = "";
  self2.metadata["config_epoch"] = "1";

  auto transport2 = gossip::net::transport_factory::create_transport(
      gossip::net::transport_type::tcp, "127.0.0.1", 19002);
  auto transport2_shared = std::shared_ptr<gossip::net::transport>(transport2.release());

  auto core2 = std::make_shared<gossip_core>(
      self2,
      [transport2_shared](const gossip_message& msg, const node_view& target) {
        std::cout << "[node2] SENDING to " << target.ip << ":" << target.port 
                  << ", entries=" << msg.entries.size() << std::endl;
        for (size_t i = 0; i < msg.entries.size(); ++i) {
          std::cout << "[node2]   entry[" << i << "]: id=" << NodeIdToString(msg.entries[i].id);
          if (msg.entries[i].metadata.count("slots")) {
            std::cout << ", slots='" << msg.entries[i].metadata.at("slots") << "'";
          }
          std::cout << std::endl;
        }
        transport2_shared->send_message(msg, target);
      },
      [](const node_view& node, node_status old_status) {
        node2_events++;
        std::cout << "[node2] EVENT: id=" << NodeIdToString(node.id)
                  << ", ip=" << node.ip << ":" << node.port
                  << ", status " << static_cast<int>(old_status)
                  << " -> " << static_cast<int>(node.status)
                  << ", heartbeat=" << node.heartbeat
                  << ", config_epoch=" << node.config_epoch << std::endl;
        print_metadata("  [node2] " + NodeIdToString(node.id), node.metadata);
      });

  transport2_shared->set_gossip_core(core2);
  auto serializer2 = std::make_unique<gossip::net::json_serializer>();
  transport2_shared->set_serializer(std::move(serializer2));
  
  if (transport2_shared->start() != gossip::net::error_code::success) {
    std::cerr << "Failed to start transport2" << std::endl;
    return 1;
  }

  std::cout << "\n=== Node 1 meets Node 2 with TEMPORARY id (based on IP:port) ===" << std::endl;
  node_view temp_node2;
  // Generate temporary node_id from IP:port (same as simple_metadata_test)
  std::string temp_id_str = "meet_127.0.0.1_19002";
  ParseNodeIdFromHex(StringToHex(temp_id_str).substr(0, 32), temp_node2.id);
  temp_node2.ip = "127.0.0.1";
  temp_node2.port = 19002;
  temp_node2.status = node_status::unknown;
  temp_node2.heartbeat = 0;
  temp_node2.version = 0;
  
  std::cout << "[node1] Meeting node2 with temp id: " << NodeIdToString(temp_node2.id) << std::endl;
  core1->meet(temp_node2);

  std::cout << "\n=== Running gossip ticks for 2 seconds ===" << std::endl;
  for (int i = 0; i < 20; ++i) {
    core1->tick();
    core2->tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "\n=== Node 2 updates slot metadata (slots 4-7) ===" << std::endl;
  std::map<std::string, std::string> metadata;
  metadata["slots"] = "4-7";
  metadata["config_epoch"] = "2";
  core2->update_self_metadata(metadata);
  
  std::cout << "[node2] Updated self metadata: slots='4-7', config_epoch=2" << std::endl;
  
  auto self2_after = core2->self();
  std::cout << "[node2] After update, self.metadata['slots'] = '" << self2_after.metadata.at("slots") << "'" << std::endl;

  std::cout << "\n=== Running gossip ticks for 3 more seconds ===" << std::endl;
  for (int i = 0; i < 30; ++i) {
    core1->tick();
    core2->tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "\n=== RESULTS ===" << std::endl;
  std::cout << "Node1 events received: " << node1_events.load() << std::endl;
  std::cout << "Node2 events received: " << node2_events.load() << std::endl;
  std::cout << "Node1 received node2's REAL ID: " << (node2_received_node1_real_id ? "YES" : "NO") << std::endl;
  std::cout << "Node1 received slot metadata: " << (node1_received_slot_metadata ? "YES" : "NO") << std::endl;

  auto nodes1 = core1->get_nodes();
  std::cout << "\nNode1 knows " << nodes1.size() << " nodes:" << std::endl;
  for (const auto& node : nodes1) {
    std::cout << "  - id=" << NodeIdToString(node.id) 
              << ", ip=" << node.ip << ":" << node.port
              << ", status=" << static_cast<int>(node.status);
    if (node.metadata.count("slots")) {
      std::cout << ", slots='" << node.metadata.at("slots") << "'";
    }
    std::cout << std::endl;
  }

  transport1_shared->stop();
  transport2_shared->stop();

  if (node2_received_node1_real_id && node1_received_slot_metadata) {
    std::cout << "\n✅ SUCCESS: Temporary ID was updated to real ID and metadata was propagated!" << std::endl;
    return 0;
  } else {
    std::cout << "\n❌ FAILED: ";
    if (!node2_received_node1_real_id) {
      std::cout << "Node1 did not receive node2's real ID. ";
    }
    if (!node1_received_slot_metadata) {
      std::cout << "Node1 did not receive slot metadata. ";
    }
    std::cout << std::endl;
    return 1;
  }
}
