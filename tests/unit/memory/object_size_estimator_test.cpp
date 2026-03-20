// =============================================================================
// Object Size Estimator Unit Tests
// =============================================================================

#include <gtest/gtest.h>
#include "astra/core/memory/object_size_estimator.hpp"

namespace astra::core::memory {

class ObjectSizeEstimatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Set up common test data
    test_key_ = "test_key";
    test_value_ = "test_value";
  }

  std::string test_key_;
  std::string test_value_;
};

// Test EstimateStringSize
TEST_F(ObjectSizeEstimatorTest, EstimateStringSize) {
  // Key: "test_key" (8 chars), Value: "test_value" (10 chars)
  uint32_t size = ObjectSizeEstimator::EstimateStringSize(test_key_, test_value_);
  EXPECT_GT(size, 0);

  // The size should be reasonable (not too large, not too small)
  // Includes key, value, metadata overhead
  EXPECT_LT(size, 1000);
}

TEST_F(ObjectSizeEstimatorTest, EstimateStringSize_LargeValue) {
  std::string large_value(1000, 'x');
  uint32_t size = ObjectSizeEstimator::EstimateStringSize(test_key_, large_value);
  EXPECT_GT(size, 1000);  // Should be at least the value size
  EXPECT_LT(size, 2000);  // But not too much larger
}

TEST_F(ObjectSizeEstimatorTest, EstimateStringSize_EmptyValue) {
  uint32_t size = ObjectSizeEstimator::EstimateStringSize(test_key_, "");
  EXPECT_GT(size, 0);  // Even empty value has overhead
  EXPECT_LT(size, 100);
}

// Test EstimateStringDelta
TEST_F(ObjectSizeEstimatorTest, EstimateStringDelta) {
  std::string old_value = "old_value";
  std::string new_value = "new_value";

  int32_t delta = ObjectSizeEstimator::EstimateStringDelta(test_key_, old_value, new_value);
  // Delta should be small for similar sized values
  EXPECT_GE(delta, -100);
  EXPECT_LE(delta, 100);
}

TEST_F(ObjectSizeEstimatorTest, EstimateStringDelta_Increase) {
  std::string old_value = "old";
  std::string new_value = "new_value_that_is_longer";

  int32_t delta = ObjectSizeEstimator::EstimateStringDelta(test_key_, old_value, new_value);
  EXPECT_GT(delta, 0);  // Positive delta for larger new value
}

TEST_F(ObjectSizeEstimatorTest, EstimateStringDelta_Decrease) {
  std::string old_value = "old_value_that_is_longer";
  std::string new_value = "new";

  int32_t delta = ObjectSizeEstimator::EstimateStringDelta(test_key_, old_value, new_value);
  EXPECT_LT(delta, 0);  // Negative delta for smaller new value
}

// Test EstimateHashSize
TEST_F(ObjectSizeEstimatorTest, EstimateHashSize) {
  std::vector<std::pair<std::string, std::string>> fields = {
    {"field1", "value1"},
    {"field2", "value2"},
    {"field3", "value3"}
  };

  uint32_t size = ObjectSizeEstimator::EstimateHashSize(test_key_, fields);
  EXPECT_GT(size, 0);

  // Size should increase with more fields
  uint32_t larger_size = ObjectSizeEstimator::EstimateHashSize(
    test_key_, {{"f1", "v1"}, {"f2", "v2"}, {"f3", "v3"}, {"f4", "v4"}});
  EXPECT_GT(larger_size, size);
}

TEST_F(ObjectSizeEstimatorTest, EstimateHashSize_EmptyHash) {
  std::vector<std::pair<std::string, std::string>> empty_fields;
  uint32_t size = ObjectSizeEstimator::EstimateHashSize(test_key_, empty_fields);
  EXPECT_GT(size, 0);  // Even empty hash has overhead
  EXPECT_LT(size, 200);
}

// Test EstimateHashFieldSize
TEST_F(ObjectSizeEstimatorTest, EstimateHashFieldSize) {
  uint32_t size = ObjectSizeEstimator::EstimateHashFieldSize("field", "value");
  EXPECT_GT(size, 0);
  EXPECT_LT(size, 100);
}

// Test EstimateListSize
TEST_F(ObjectSizeEstimatorTest, EstimateListSize) {
  std::vector<std::string> elements = {"elem1", "elem2", "elem3"};

  uint32_t size = ObjectSizeEstimator::EstimateListSize(test_key_, elements);
  EXPECT_GT(size, 0);

  // Size should increase with more elements
  uint32_t larger_size = ObjectSizeEstimator::EstimateListSize(
    test_key_, {"elem1", "elem2", "elem3", "elem4"});
  EXPECT_GT(larger_size, size);
}

TEST_F(ObjectSizeEstimatorTest, EstimateListSize_EmptyList) {
  std::vector<std::string> empty_elements;
  uint32_t size = ObjectSizeEstimator::EstimateListSize(test_key_, empty_elements);
  EXPECT_GT(size, 0);  // Even empty list has overhead
  EXPECT_LT(size, 200);
}

// Test EstimateListElementSize
TEST_F(ObjectSizeEstimatorTest, EstimateListElementSize) {
  uint32_t size = ObjectSizeEstimator::EstimateListElementSize("element");
  EXPECT_GT(size, 0);
  EXPECT_LT(size, 100);
}

// Test EstimateSetSize
TEST_F(ObjectSizeEstimatorTest, EstimateSetSize) {
  std::vector<std::string> members = {"member1", "member2", "member3"};

  uint32_t size = ObjectSizeEstimator::EstimateSetSize(test_key_, members);
  EXPECT_GT(size, 0);

  // Size should increase with more members
  uint32_t larger_size = ObjectSizeEstimator::EstimateSetSize(
    test_key_, {"member1", "member2", "member3", "member4"});
  EXPECT_GT(larger_size, size);
}

TEST_F(ObjectSizeEstimatorTest, EstimateSetSize_EmptySet) {
  std::vector<std::string> empty_members;
  uint32_t size = ObjectSizeEstimator::EstimateSetSize(test_key_, empty_members);
  EXPECT_GT(size, 0);  // Even empty set has overhead
  EXPECT_LT(size, 200);
}

// Test EstimateSetMemberSize
TEST_F(ObjectSizeEstimatorTest, EstimateSetMemberSize) {
  uint32_t size = ObjectSizeEstimator::EstimateSetMemberSize("member");
  EXPECT_GT(size, 0);
  EXPECT_LT(size, 100);
}

// Test EstimateZSetSize
TEST_F(ObjectSizeEstimatorTest, EstimateZSetSize) {
  std::vector<std::pair<std::string, double>> members = {
    {"member1", 1.0},
    {"member2", 2.0},
    {"member3", 3.0}
  };

  uint32_t size = ObjectSizeEstimator::EstimateZSetSize(test_key_, members);
  EXPECT_GT(size, 0);

  // Size should increase with more members
  uint32_t larger_size = ObjectSizeEstimator::EstimateZSetSize(
    test_key_, {{"member1", 1.0}, {"member2", 2.0}, {"member3", 3.0}, {"member4", 4.0}});
  EXPECT_GT(larger_size, size);
}

TEST_F(ObjectSizeEstimatorTest, EstimateZSetSize_EmptyZSet) {
  std::vector<std::pair<std::string, double>> empty_members;
  uint32_t size = ObjectSizeEstimator::EstimateZSetSize(test_key_, empty_members);
  EXPECT_GT(size, 0);  // Even empty zset has overhead
  EXPECT_LT(size, 200);
}

// Test EstimateZSetMemberSize
TEST_F(ObjectSizeEstimatorTest, EstimateZSetMemberSize) {
  uint32_t size = ObjectSizeEstimator::EstimateZSetMemberSize("member");
  EXPECT_GT(size, 0);
  EXPECT_LT(size, 100);
}

// Test EstimateStreamSize
TEST_F(ObjectSizeEstimatorTest, EstimateStreamSize) {
  std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> entries = {
    {"id1", {{"field1", "value1"}, {"field2", "value2"}}},
    {"id2", {{"field3", "value3"}}}
  };

  uint32_t size = ObjectSizeEstimator::EstimateStreamSize(test_key_, entries);
  EXPECT_GT(size, 0);

  // Size should increase with more entries
  std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> larger_entries = {
    {"id1", {{"field1", "value1"}}},
    {"id2", {{"field2", "value2"}}},
    {"id3", {{"field3", "value3"}}}
  };
  uint32_t larger_size = ObjectSizeEstimator::EstimateStreamSize(test_key_, larger_entries);
  EXPECT_GT(larger_size, size);
}

TEST_F(ObjectSizeEstimatorTest, EstimateStreamSize_EmptyStream) {
  std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> empty_entries;
  uint32_t size = ObjectSizeEstimator::EstimateStreamSize(test_key_, empty_entries);
  EXPECT_GT(size, 0);  // Even empty stream has overhead
  EXPECT_LT(size, 200);
}

// Test EstimateStreamEntrySize
TEST_F(ObjectSizeEstimatorTest, EstimateStreamEntrySize) {
  std::vector<std::pair<std::string, std::string>> fields = {
    {"field1", "value1"},
    {"field2", "value2"}
  };

  uint32_t size = ObjectSizeEstimator::EstimateStreamEntrySize("entry_id", fields);
  EXPECT_GT(size, 0);
}

// Test EstimateMetadataSize
TEST_F(ObjectSizeEstimatorTest, EstimateMetadataSize) {
  uint32_t size = ObjectSizeEstimator::EstimateMetadataSize(test_key_);
  EXPECT_GT(size, 0);
  EXPECT_LT(size, 100);

  // Larger key should have larger size
  uint32_t larger_size = ObjectSizeEstimator::EstimateMetadataSize("a_very_long_key_name_with_many_characters");
  EXPECT_GT(larger_size, size);
}

// Test consistency across data types
TEST_F(ObjectSizeEstimatorTest, ConsistencyAcrossDataTypes) {
  // Different data types with similar content should have comparable sizes
  std::string value = "test_value";

  uint32_t string_size = ObjectSizeEstimator::EstimateStringSize(test_key_, value);
  uint32_t list_size = ObjectSizeEstimator::EstimateListSize(test_key_, {value});
  uint32_t set_size = ObjectSizeEstimator::EstimateSetSize(test_key_, {value});

  // All should be similar (within reasonable range)
  uint32_t max_size = std::max({string_size, list_size, set_size});
  uint32_t min_size = std::min({string_size, list_size, set_size});
  EXPECT_LT(max_size - min_size, 500);  // Difference should be less than 500 bytes
}

// Test EstimateTotalSize
TEST_F(ObjectSizeEstimatorTest, EstimateTotalSize) {
  std::string data = "test_data";
  uint32_t total_size = ObjectSizeEstimator::EstimateTotalSize(test_key_, data.size());

  // Total size should be metadata size + data size
  uint32_t metadata_size = ObjectSizeEstimator::EstimateMetadataSize(test_key_);
  EXPECT_GT(total_size, metadata_size);
}

}  // namespace astra::core::memory