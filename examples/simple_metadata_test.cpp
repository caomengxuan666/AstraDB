#include <chrono>
#include <core/gossip_core.hpp>
#include <iostream>
#include <map>
#include <memory>
#include <net/json_serializer.hpp>
#include <net/transport_factory.hpp>
#include <thread>

using namespace libgossip;

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

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cout << "Usage: " << argv[0] << " <node_id> <port>" << std::endl;
    std::cout << "Example: " << argv[0] << " node1 18001" << std::endl;
    return 1;
  }

  std::string node_id = argv[1];
  uint16_t port = std::stoi(argv[2]);

  std::cout << "[" << node_id << "] Starting on port " << port << std::endl;

  // Configure self node
  node_view self;
  ParseNodeIdFromHex(StringToHex(node_id).substr(0, 32), self.id);
  self.ip = "127.0.0.1";
  self.port = port;
  self.status = node_status::online;
  self.heartbeat = 1;
  self.version = 1;
  self.config_epoch = 1;

  std::cout << "[" << node_id << "] Self: " << NodeIdToString(self.id) << " "
            << self.ip << ":" << self.port << std::endl;

  // Create transport using factory first

  auto transport = gossip::net::transport_factory::create_transport(
      gossip::net::transport_type::tcp, "127.0.0.1", port);

  if (!transport) {
    std::cerr << "[" << node_id << "] Failed to create transport" << std::endl;

    return 1;
  }

  // Create shared_ptr for transport (needed for lambda capture)

  auto transport_shared =
      std::shared_ptr<gossip::net::transport>(transport.release());

  // Create gossip core with callbacks

  auto core = std::make_shared<gossip_core>(

      self,

      [transport_shared, node_id](const gossip_message& msg,
                                  const node_view& target) {
        // Send callback - will be called by gossip_core to send messages

        std::cout << "[" << node_id << "] SENDING message to " << target.ip
                  << ":" << target.port

                  << " entries=" << msg.entries.size() << std::endl;

        transport_shared->send_message(msg, target);
      },

      [node_id](const node_view& node, node_status old_status) {
        // Event callback - will be called when node status/metadata changes

        std::cout << "[" << node_id << "] EVENT: " << NodeIdToString(node.id)

                  << " status " << static_cast<int>(old_status)

                  << " -> " << static_cast<int>(node.status)

                  << " heartbeat=" << node.heartbeat

                  << " config_epoch=" << node.config_epoch << std::endl;

        print_metadata("  [" + node_id + "] " + NodeIdToString(node.id),
                       node.metadata);
      });

  // Set gossip core on transport (this will handle received messages)

  transport_shared->set_gossip_core(core);

  // Set serializer

  transport_shared->set_serializer(
      std::make_unique<gossip::net::json_serializer>());

  // Start transport

  auto ec = transport_shared->start();

  if (ec != gossip::net::error_code::success) {
    std::cerr << "[" << node_id
              << "] Failed to start transport: " << static_cast<int>(ec)
              << std::endl;

    return 1;
  }

  std::cout << "[" << node_id << "] Transport started successfully"
            << std::endl;
  std::cout << "[" << node_id << "] Initialized. Waiting for commands..."
            << std::endl;
  std::cout << "Commands:" << std::endl;
  std::cout << "  meet <ip> <port>  - Meet another node" << std::endl;
  std::cout << "  update <key> <value> - Update metadata" << std::endl;
  std::cout << "  self - Show self metadata" << std::endl;
  std::cout << "  tick - Trigger gossip tick" << std::endl;
  std::cout << "  quit - Exit" << std::endl;

  std::string line;
  while (true) {
    std::cout << "[" << node_id << "] > ";
    std::getline(std::cin, line);

    if (line.empty()) continue;
    if (line == "quit") break;

    if (line == "self") {
      auto s = core->self();
      std::cout << "[" << node_id << "] Self: " << NodeIdToString(s.id) << " "
                << s.ip << ":" << s.port << " heartbeat=" << s.heartbeat
                << " config_epoch=" << s.config_epoch << std::endl;
      print_metadata("  [" + node_id + "] Self", s.metadata);
      continue;
    }

    if (line == "tick") {
      core->tick();
      std::cout << "[" << node_id << "] Tick triggered" << std::endl;
      continue;
    }

    size_t space1 = line.find(' ');
    if (space1 == std::string::npos) {
      std::cout << "Invalid command" << std::endl;
      continue;
    }

    std::string cmd = line.substr(0, space1);
    std::string rest = line.substr(space1 + 1);

    if (cmd == "meet") {
      size_t space2 = rest.find(' ');
      if (space2 == std::string::npos) {
        std::cout << "Usage: meet <ip> <port>" << std::endl;
        continue;
      }
      std::string ip = rest.substr(0, space2);
      uint16_t meet_port = std::stoi(rest.substr(space2 + 1));

      // Create node_view for meet
      node_view target;
      std::string target_id_str =
          "meet_" + ip + "_" + std::to_string(meet_port);
      ParseNodeIdFromHex(StringToHex(target_id_str).substr(0, 32), target.id);
      target.ip = ip;
      target.port = meet_port;
      target.status = node_status::unknown;
      target.heartbeat = 0;
      target.version = 0;

      core->meet(target);
      std::cout << "[" << node_id << "] Meeting " << ip << ":" << meet_port
                << std::endl;
    } else if (cmd == "update") {
      size_t space2 = rest.find(' ');
      if (space2 == std::string::npos) {
        std::cout << "Usage: update <key> <value>" << std::endl;
        continue;
      }
      std::string key = rest.substr(0, space2);
      std::string value = rest.substr(space2 + 1);

      std::map<std::string, std::string> metadata;
      metadata[key] = value;
      metadata["updated_at"] = std::to_string(
          std::chrono::system_clock::now().time_since_epoch().count());

      core->update_self_metadata(metadata);
      std::cout << "[" << node_id << "] Updated metadata: " << key << "="
                << value << std::endl;

      auto s = core->self();
      print_metadata("  [" + node_id + "] Self after update", s.metadata);
    } else {
      std::cout << "Unknown command: " << cmd << std::endl;
    }
  }

  std::cout << "[" << node_id << "] Exiting..." << std::endl;
  return 0;
}
