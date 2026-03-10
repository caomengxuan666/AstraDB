#include <gtest/gtest.h>

#include "astra/container/zset/btree_zset.hpp"

namespace astra::container {

TEST(ZSetTest, BasicOperations) {
  StringZSet zset(100);

  // Test Add
  EXPECT_TRUE(zset.Add("member1", 10.5));
  EXPECT_TRUE(zset.Add("member2", 20.3));
  EXPECT_TRUE(zset.Add("member3", 15.7));

  // Test Update
  EXPECT_FALSE(zset.Add("member1", 30.5));  // Update existing

  // Test GetScore
  auto score = zset.GetScore("member1");
  EXPECT_TRUE(score.has_value());
  EXPECT_DOUBLE_EQ(*score, 30.5);

  score = zset.GetScore("nonexistent");
  EXPECT_FALSE(score.has_value());

  // Test Size
  EXPECT_EQ(zset.Size(), 3);
  EXPECT_FALSE(zset.Empty());

  // Test Contains
  EXPECT_TRUE(zset.Contains("member1"));
  EXPECT_FALSE(zset.Contains("nonexistent"));
}

TEST(ZSetTest, RankOperations) {
  StringZSet zset(100);

  zset.Add("a", 10.0);
  zset.Add("b", 20.0);
  zset.Add("c", 30.0);
  zset.Add("d", 25.0);

  // Test GetRank
  auto rank = zset.GetRank("a");
  EXPECT_TRUE(rank.has_value());
  EXPECT_EQ(*rank, 0);  // Lowest score = rank 0

  rank = zset.GetRank("c");
  EXPECT_TRUE(rank.has_value());
  EXPECT_EQ(*rank, 3);  // Highest score = rank 3

  // Test reverse rank
  rank = zset.GetRank("a", true);
  EXPECT_TRUE(rank.has_value());
  EXPECT_EQ(*rank, 3);  // Highest score = reverse rank 0

  rank = zset.GetRank("c", true);
  EXPECT_TRUE(rank.has_value());
  EXPECT_EQ(*rank, 0);  // Lowest score = reverse rank 3
}

TEST(ZSetTest, GetByRank) {
  StringZSet zset(100);

  zset.Add("a", 10.0);
  zset.Add("b", 20.0);
  zset.Add("c", 30.0);

  // Test GetByRank
  auto member = zset.GetByRank(0);
  EXPECT_TRUE(member.has_value());
  EXPECT_EQ(*member, "a");

  member = zset.GetByRank(2);
  EXPECT_TRUE(member.has_value());
  EXPECT_EQ(*member, "c");

  // Test out of range
  member = zset.GetByRank(10);
  EXPECT_FALSE(member.has_value());
}

TEST(ZSetTest, GetScoreByRank) {
  StringZSet zset(100);

  zset.Add("a", 10.0);
  zset.Add("b", 20.0);
  zset.Add("c", 30.0);

  auto score = zset.GetScoreByRank(0);
  EXPECT_TRUE(score.has_value());
  EXPECT_DOUBLE_EQ(*score, 10.0);

  score = zset.GetScoreByRank(2);
  EXPECT_TRUE(score.has_value());
  EXPECT_DOUBLE_EQ(*score, 30.0);
}

TEST(ZSetTest, RangeByRank) {
  StringZSet zset(100);

  for (int i = 0; i < 10; ++i) {
    zset.Add("member" + std::to_string(i), i * 10.0);
  }

  auto range = zset.GetRangeByRank(0, 4);
  EXPECT_EQ(range.size(), 5);
  EXPECT_EQ(range[0].first, "member0");
  EXPECT_EQ(range[4].first, "member4");

  // Test with scores
  range = zset.GetRangeByRank(0, 4, false, true);
  EXPECT_EQ(range.size(), 5);
  EXPECT_DOUBLE_EQ(range[0].second, 0.0);
  EXPECT_DOUBLE_EQ(range[4].second, 40.0);

  // Test reverse
  range = zset.GetRangeByRank(0, 4, true);
  EXPECT_EQ(range.size(), 5);
  EXPECT_EQ(range[0].first, "member9");
  EXPECT_EQ(range[4].first, "member5");
}

TEST(ZSetTest, RangeByScore) {
  StringZSet zset(100);

  for (int i = 0; i < 10; ++i) {
    zset.Add("member" + std::to_string(i), i * 10.0);
  }

  auto range = zset.GetRangeByScore(20.0, 50.0);
  EXPECT_EQ(range.size(), 4);
  EXPECT_EQ(range[0].first, "member2");
  EXPECT_EQ(range[3].first, "member5");

  // Test with scores
  range = zset.GetRangeByScore(20.0, 50.0, true);
  EXPECT_EQ(range.size(), 4);
  EXPECT_DOUBLE_EQ(range[0].second, 20.0);
  EXPECT_DOUBLE_EQ(range[3].second, 50.0);
}

TEST(ZSetTest, CountRange) {
  StringZSet zset(100);

  for (int i = 0; i < 10; ++i) {
    zset.Add("member" + std::to_string(i), i * 10.0);
  }

  uint64_t count = zset.CountRange(20.0, 50.0);
  EXPECT_EQ(count, 4);

  count = zset.CountRange(100.0, 200.0);
  EXPECT_EQ(count, 0);
}

TEST(ZSetTest, Remove) {
  StringZSet zset(100);

  zset.Add("member1", 10.0);
  zset.Add("member2", 20.0);
  zset.Add("member3", 30.0);

  EXPECT_TRUE(zset.Remove("member2"));
  EXPECT_EQ(zset.Size(), 2);
  EXPECT_FALSE(zset.Contains("member2"));

  EXPECT_FALSE(zset.Remove("nonexistent"));
}

TEST(ZSetTest, RemoveRangeByScore) {
  StringZSet zset(100);

  for (int i = 0; i < 10; ++i) {
    zset.Add("member" + std::to_string(i), i * 10.0);
  }

  uint64_t count = zset.RemoveRangeByScore(20.0, 50.0);
  EXPECT_EQ(count, 4);
  EXPECT_EQ(zset.Size(), 6);
}

TEST(ZSetTest, Clear) {
  StringZSet zset(100);

  zset.Add("member1", 10.0);
  zset.Add("member2", 20.0);

  EXPECT_EQ(zset.Size(), 2);

  zset.Clear();

  EXPECT_EQ(zset.Size(), 0);
  EXPECT_TRUE(zset.Empty());
}

TEST(ZSetTest, LargeDataset) {
  StringZSet zset(10000);

  // Add 1000 members
  for (int i = 0; i < 1000; ++i) {
    zset.Add("member" + std::to_string(i), static_cast<double>(i));
  }

  EXPECT_EQ(zset.Size(), 1000);

  // Test rank operations
  auto rank = zset.GetRank("member500");
  EXPECT_TRUE(rank.has_value());
  EXPECT_EQ(*rank, 500);

  // Test range operations
  auto range = zset.GetRangeByRank(0, 99);
  EXPECT_EQ(range.size(), 100);

  // Test remove
  for (int i = 0; i < 100; ++i) {
    zset.Remove("member" + std::to_string(i));
  }

  EXPECT_EQ(zset.Size(), 900);
}

TEST(ZSetTest, DuplicateScores) {
  StringZSet zset(100);

  // Add members with same score
  zset.Add("a", 10.0);
  zset.Add("b", 10.0);
  zset.Add("c", 10.0);

  EXPECT_EQ(zset.Size(), 3);

  // They should be ordered by member name
  auto range = zset.GetRangeByRank(0, 2);
  EXPECT_EQ(range.size(), 3);
  EXPECT_EQ(range[0].first, "a");
  EXPECT_EQ(range[1].first, "b");
  EXPECT_EQ(range[2].first, "c");
}

}  // namespace astra::container
