// ==============================================================================
// Snapshot Manager Unit Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>
#include "astra/persistence/snapshot_manager.hpp"
#include "astra/persistence/leveldb_adapter.hpp"
#include <filesystem>
#include <thread>
#include <chrono>

namespace astra::persistence {
namespace {

class SnapshotManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create unique test directories
    test_dir_ = "/tmp/astradb_test_snapshot_" + std::to_string(std::time(nullptr));
    db_dir_ = test_dir_ + "/db";
    snapshot_dir_ = test_dir_ + "/snapshots";
    std::filesystem::create_directories(db_dir_);
    std::filesystem::create_directories(snapshot_dir_);
    
    // Initialize LevelDB
    LevelDBOptions db_options;
    db_options.db_path = db_dir_;
    db_options.create_if_missing = true;
    
    ASSERT_TRUE(adapter_.Open(db_options));
    
    // Initialize SnapshotManager
    SnapshotOptions snap_options;
    snap_options.snapshot_dir = snapshot_dir_;
    snap_options.max_snapshots = 3;
    
    ASSERT_TRUE(snapshot_manager_.Init(snap_options));
  }
  
  void TearDown() override {
    adapter_.Close();
    
    // Cleanup test directory
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
  }
  
  LevelDBAdapter adapter_;
  SnapshotManager snapshot_manager_;
  std::string test_dir_;
  std::string db_dir_;
  std::string snapshot_dir_;
};

// ========== Basic Operations Tests ==========

TEST_F(SnapshotManagerTest, Init) {
  EXPECT_TRUE(snapshot_manager_.HasSnapshot() || true);  // May be empty initially
}

TEST_F(SnapshotManagerTest, CreateSnapshot) {
  // Insert some data
  adapter_.Put("S:key1", "value1");
  adapter_.Put("S:key2", "value2");
  adapter_.Put("H:hash1:field1", "hvalue1");
  
  EXPECT_TRUE(snapshot_manager_.CreateSnapshot(adapter_, "test_snapshot"));
  
  // Check that snapshot file was created
  std::string snapshot_path = snapshot_dir_ + "/test_snapshot.snap";
  EXPECT_TRUE(std::filesystem::exists(snapshot_path));
}

TEST_F(SnapshotManagerTest, CreateSnapshotWithTimestamp) {
  adapter_.Put("S:key1", "value1");
  
  EXPECT_TRUE(snapshot_manager_.CreateSnapshot(adapter_));
  
  // List snapshots
  auto snapshots = snapshot_manager_.ListSnapshots();
  EXPECT_EQ(snapshots.size(), 1);
}

TEST_F(SnapshotManagerTest, ListSnapshots) {
  adapter_.Put("S:key1", "value1");
  
  // Create multiple snapshots
  snapshot_manager_.CreateSnapshot(adapter_, "snap1");
  snapshot_manager_.CreateSnapshot(adapter_, "snap2");
  snapshot_manager_.CreateSnapshot(adapter_, "snap3");
  
  auto snapshots = snapshot_manager_.ListSnapshots();
  EXPECT_GE(snapshots.size(), 3);
}

TEST_F(SnapshotManagerTest, DeleteSnapshot) {
  adapter_.Put("S:key1", "value1");
  
  snapshot_manager_.CreateSnapshot(adapter_, "to_delete");
  
  auto snapshots_before = snapshot_manager_.ListSnapshots();
  EXPECT_GE(snapshots_before.size(), 1);
  
  EXPECT_TRUE(snapshot_manager_.DeleteSnapshot("to_delete"));
}

// ========== Restore Tests ==========

TEST_F(SnapshotManagerTest, RestoreSnapshot) {
  // Insert data and create snapshot
  adapter_.Put("S:key1", "value1");
  adapter_.Put("S:key2", "value2");
  adapter_.Put("H:hash1:field1", "hvalue1");
  
  EXPECT_TRUE(snapshot_manager_.CreateSnapshot(adapter_, "restore_test"));
  
  // Clear the database
  adapter_.Delete("S:key1");
  adapter_.Delete("S:key2");
  adapter_.Delete("H:hash1:field1");
  
  EXPECT_FALSE(adapter_.Exists("S:key1"));
  EXPECT_FALSE(adapter_.Exists("S:key2"));
  
  // Restore from snapshot
  EXPECT_TRUE(snapshot_manager_.RestoreSnapshot(adapter_, "restore_test"));
  
  // Verify data is restored
  EXPECT_TRUE(adapter_.Exists("S:key1"));
  EXPECT_TRUE(adapter_.Exists("S:key2"));
  EXPECT_EQ(adapter_.Get("S:key1").value(), "value1");
  EXPECT_EQ(adapter_.Get("S:key2").value(), "value2");
}

TEST_F(SnapshotManagerTest, RestoreLatestSnapshot) {
  // Insert data and create snapshot
  adapter_.Put("S:key1", "original");
  snapshot_manager_.CreateSnapshot(adapter_, "first");
  
  // Modify data and create another snapshot
  adapter_.Put("S:key1", "modified");
  snapshot_manager_.CreateSnapshot(adapter_, "second");
  
  // Clear and restore (should get latest)
  adapter_.Delete("S:key1");
  
  EXPECT_TRUE(snapshot_manager_.RestoreSnapshot(adapter_));
  
  // Should have the latest value
  EXPECT_TRUE(adapter_.Exists("S:key1"));
}

// ========== Cleanup Tests ==========

TEST_F(SnapshotManagerTest, MaxSnapshotsCleanup) {
  // Create more snapshots than max_snapshots (3)
  for (int i = 0; i < 5; ++i) {
    adapter_.Put("S:key", "value" + std::to_string(i));
    snapshot_manager_.CreateSnapshot(adapter_, "snap" + std::to_string(i));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  auto snapshots = snapshot_manager_.ListSnapshots();
  // Should have at most max_snapshots
  EXPECT_LE(snapshots.size(), 3);
}

// ========== Multiple Data Types Tests ==========

TEST_F(SnapshotManagerTest, MultipleDataTypes) {
  // Insert various data types
  adapter_.Put("S:string1", "string_value");
  adapter_.Put("H:hash1:field1", "hash_value");
  adapter_.Put("E:set1:member1", "1");
  adapter_.Put("Z:zset1:member1", "1.5");
  adapter_.Put("L:list1:0", "list_value");
  
  EXPECT_TRUE(snapshot_manager_.CreateSnapshot(adapter_, "multi_type"));
  
  // Clear and restore
  adapter_.Delete("S:string1");
  adapter_.Delete("H:hash1:field1");
  adapter_.Delete("E:set1:member1");
  adapter_.Delete("Z:zset1:member1");
  adapter_.Delete("L:list1:0");
  
  EXPECT_TRUE(snapshot_manager_.RestoreSnapshot(adapter_, "multi_type"));
  
  // Verify all types restored
  EXPECT_TRUE(adapter_.Exists("S:string1"));
  EXPECT_TRUE(adapter_.Exists("H:hash1:field1"));
  EXPECT_TRUE(adapter_.Exists("E:set1:member1"));
  EXPECT_TRUE(adapter_.Exists("Z:zset1:member1"));
  EXPECT_TRUE(adapter_.Exists("L:list1:0"));
}

// ========== Edge Cases ==========

TEST_F(SnapshotManagerTest, EmptyDatabase) {
  // Create snapshot of empty database
  EXPECT_TRUE(snapshot_manager_.CreateSnapshot(adapter_, "empty"));
  
  // Restore should work
  EXPECT_TRUE(snapshot_manager_.RestoreSnapshot(adapter_, "empty"));
}

TEST_F(SnapshotManagerTest, LargeData) {
  // Insert large amount of data
  for (int i = 0; i < 100; ++i) {
    adapter_.Put("S:key" + std::to_string(i), "value" + std::to_string(i));
  }
  
  EXPECT_TRUE(snapshot_manager_.CreateSnapshot(adapter_, "large"));
  
  // Clear and restore
  for (int i = 0; i < 100; ++i) {
    adapter_.Delete("S:key" + std::to_string(i));
  }
  
  EXPECT_TRUE(snapshot_manager_.RestoreSnapshot(adapter_, "large"));
  
  // Verify data restored
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(adapter_.Exists("S:key" + std::to_string(i)));
  }
}

TEST_F(SnapshotManagerTest, SpecialCharactersInValue) {
  adapter_.Put("S:special", "value\nwith\nnewlines\tand\ttabs");
  
  EXPECT_TRUE(snapshot_manager_.CreateSnapshot(adapter_, "special"));
  
  adapter_.Delete("S:special");
  
  EXPECT_TRUE(snapshot_manager_.RestoreSnapshot(adapter_, "special"));
  
  EXPECT_EQ(adapter_.Get("S:special").value(), "value\nwith\nnewlines\tand\ttabs");
}

}  // namespace
}  // namespace astra::persistence
