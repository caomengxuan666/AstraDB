// ==============================================================================
// FlatBuffers Cluster Communication Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "astra/cluster/cluster_flatbuffers.hpp"
#include "astra/cluster/gossip_manager.hpp"
#include "astra/cluster/shard_manager.hpp"

namespace astra::cluster {
namespace test {

// Test fixture for cluster FlatBuffers
class ClusterFlatbuffersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Initialize test node ID
    test_node_id_ = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                     0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    
    // Initialize test node info
    test_node_info_.id = test_node_id_;
    test_node_info_.ip = "192.168.1.100";
    test_node_info_.port = 6379;
    test_node_info_.gossip_port = 7379;
    test_node_info_.role = "master";
    test_node_info_.status = libgossip::node_status::online;
    test_node_info_.config_epoch = 12345;
    test_node_info_.heartbeat = 67890;
    test_node_info_.shard_count = 4;
    test_node_info_.memory_used = 1024 * 1024 * 100;  // 100MB
    test_node_info_.keys_count = 1000000;
    test_node_info_.region = "us-west-1";
    test_node_info_.replication_lag_ms = 10;
  }

  NodeId test_node_id_;
  astra::cluster::NodeInfo test_node_info_;
};

// Test heartbeat serialization/deserialization
TEST_F(ClusterFlatbuffersTest, SerializeDeserializeHeartbeat) {
  uint64_t config_epoch = 12345;
  uint64_t sequence = 67890;
  
  // Serialize heartbeat
  auto data = ClusterFlatbuffers::SerializeHeartbeat(
    test_node_id_, config_epoch, sequence);
  
  ASSERT_FALSE(data.empty());
  
  // Deserialize heartbeat
  NodeId deserialized_id;
  uint64_t deserialized_epoch, deserialized_seq, timestamp;
  
  bool success = ClusterFlatbuffers::DeserializeHeartbeat(
    data.data(), data.size(),
    deserialized_id, deserialized_epoch, deserialized_seq, timestamp);
  
  ASSERT_TRUE(success);
  EXPECT_EQ(deserialized_id, test_node_id_);
  EXPECT_EQ(deserialized_epoch, config_epoch);
  EXPECT_EQ(deserialized_seq, sequence);
  EXPECT_GT(timestamp, 0);
  
  // Get message type
  AstraDB::Cluster::MessageType type;
  NodeId source;
  uint64_t seq;
  success = ClusterFlatbuffers::DeserializeMessage(
    data.data(), data.size(), type, seq, source);
  
  ASSERT_TRUE(success);
  EXPECT_EQ(type, AstraDB::Cluster::MessageType_Heartbeat);
  EXPECT_EQ(source, test_node_id_);
}

// Test node state serialization/deserialization
TEST_F(ClusterFlatbuffersTest, SerializeDeserializeNodeState) {
  // Serialize node state
  auto data = ClusterFlatbuffers::SerializeNodeState(test_node_info_);
  
  ASSERT_FALSE(data.empty());
  
  // Deserialize node state
  astra::cluster::NodeInfo deserialized_info;
  bool success = ClusterFlatbuffers::DeserializeNodeState(
    data.data(), data.size(), deserialized_info);
  
  ASSERT_TRUE(success);
  EXPECT_EQ(deserialized_info.id, test_node_info_.id);
  EXPECT_EQ(deserialized_info.ip, test_node_info_.ip);
  EXPECT_EQ(deserialized_info.port, test_node_info_.port);
  EXPECT_EQ(deserialized_info.role, test_node_info_.role);
  EXPECT_EQ(deserialized_info.status, test_node_info_.status);
  EXPECT_EQ(deserialized_info.config_epoch, test_node_info_.config_epoch);
  EXPECT_EQ(deserialized_info.shard_count, test_node_info_.shard_count);
  EXPECT_EQ(deserialized_info.keys_count, test_node_info_.keys_count);
}

// Test slot assignment serialization
TEST_F(ClusterFlatbuffersTest, SerializeDeserializeSlotAssignment) {
  uint16_t slot = 100;
  NodeId target_node = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
                       0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
  
  // Serialize slot assignment
  auto data = ClusterFlatbuffers::SerializeSlotAssignment(
    slot, test_node_id_, MigrationAction::kStable, target_node, target_node);
  
  ASSERT_FALSE(data.empty());
  
  // Get message type
  AstraDB::Cluster::MessageType type;
  NodeId source;
  uint64_t seq;
  bool success = ClusterFlatbuffers::DeserializeMessage(
    data.data(), data.size(), type, seq, source);
  
  ASSERT_TRUE(success);
  EXPECT_EQ(type, AstraDB::Cluster::MessageType_SlotAssignment);
  EXPECT_EQ(source, test_node_id_);
}

// Test data migration serialization
TEST_F(ClusterFlatbuffersTest, SerializeDeserializeDataMigration) {
  NodeId target_node = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
                       0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
  
  // Create test key-values
  std::vector<std::pair<std::string, std::string>> key_values = {
    {"key1", "value1"},
    {"key2", "value2"},
    {"key3", "value3"}
  };
  
  uint16_t slot = 200;
  uint64_t batch_id = 12345;
  bool is_last = true;
  
  // Serialize data migration
  auto data = ClusterFlatbuffers::SerializeDataMigration(
    test_node_id_, target_node, slot, key_values, batch_id, is_last);
  
  ASSERT_FALSE(data.empty());
  
  // Deserialize data migration
  NodeId source, target;
  uint16_t deserialized_slot;
  std::vector<std::pair<std::string, std::string>> deserialized_kvs;
  uint64_t deserialized_batch_id;
  bool deserialized_is_last;
  
  bool success = ClusterFlatbuffers::DeserializeDataMigration(
    data.data(), data.size(),
    source, target, deserialized_slot, deserialized_kvs,
    deserialized_batch_id, deserialized_is_last);
  
  ASSERT_TRUE(success);
  EXPECT_EQ(source, test_node_id_);
  EXPECT_EQ(target, target_node);
  EXPECT_EQ(deserialized_slot, slot);
  EXPECT_EQ(deserialized_batch_id, batch_id);
  EXPECT_EQ(deserialized_is_last, is_last);
  EXPECT_EQ(deserialized_kvs.size(), key_values.size());
  
  // Verify key-values
  for (size_t i = 0; i < key_values.size(); ++i) {
    EXPECT_EQ(deserialized_kvs[i].first, key_values[i].first);
    EXPECT_EQ(deserialized_kvs[i].second, key_values[i].second);
  }
}

// Test cluster snapshot serialization
TEST_F(ClusterFlatbuffersTest, SerializeDeserializeClusterSnapshot) {
  // Create test nodes
  std::vector<astra::cluster::NodeInfo> nodes = {test_node_info_};
  
  // Create test slot assignments
  std::vector<std::pair<uint16_t, NodeId>> slot_assignments = {
    {0, test_node_id_},
    {100, test_node_id_},
    {200, test_node_id_}
  };
  
  uint64_t config_epoch = 12345;
  uint64_t total_keys = 1000000;
  
  // Serialize cluster snapshot
  auto data = ClusterFlatbuffers::SerializeClusterSnapshot(
    nodes, slot_assignments, config_epoch, total_keys);
  
  ASSERT_FALSE(data.empty());
  
  // Get message type
  AstraDB::Cluster::MessageType type;
  NodeId source;
  uint64_t seq;
  bool success = ClusterFlatbuffers::DeserializeMessage(
    data.data(), data.size(), type, seq, source);
  
  ASSERT_TRUE(success);
  EXPECT_EQ(type, AstraDB::Cluster::MessageType_SyncResponse);
}

// Test message size retrieval
TEST_F(ClusterFlatbuffersTest, GetMessageSize) {
  auto data = ClusterFlatbuffers::SerializeHeartbeat(
    test_node_id_, 12345, 67890);
  
  ASSERT_FALSE(data.empty());
  
  size_t size = ClusterFlatbuffers::GetMessageSize(data.data());
  EXPECT_GT(size, 0);
  EXPECT_LE(size, data.size());
}

// Test empty data handling
TEST_F(ClusterFlatbuffersTest, HandleEmptyData) {
  NodeId id;
  uint64_t epoch, seq, timestamp;
  
  // Should fail with empty data
  bool success = ClusterFlatbuffers::DeserializeHeartbeat(
    nullptr, 0, id, epoch, seq, timestamp);
  
  EXPECT_FALSE(success);
}

// Test invalid data handling
TEST_F(ClusterFlatbuffersTest, HandleInvalidData) {
  std::vector<uint8_t> invalid_data = {0x01, 0x02, 0x03};
  
  NodeId id;
  uint64_t epoch, seq, timestamp;
  
  // Should fail with invalid data
  bool success = ClusterFlatbuffers::DeserializeHeartbeat(
    invalid_data.data(), invalid_data.size(), id, epoch, seq, timestamp);
  
  EXPECT_FALSE(success);
}

}  // namespace test
}  // namespace astra::cluster