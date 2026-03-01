// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/container/btree_map.h>
#include <absl/container/flat_hash_map.h>
#include <absl/hash/hash.h>
#include <absl/synchronization/mutex.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "astra/base/macros.hpp"

namespace astra::container {

// ScoredMember - Sorted Set element with score and member
struct ScoredMember {
  double score;
  std::string member;
  
  ScoredMember() : score(0.0) {}
  
  ScoredMember(double s, std::string m) : score(s), member(std::move(m)) {}
  
  // Comparison for ordering by score, then by member
  bool operator<(const ScoredMember& other) const {
    if (score != other.score) {
      return score < other.score;
    }
    return member < other.member;
  }
  
  bool operator==(const ScoredMember& other) const {
    return score == other.score && member == other.member;
  }
  
  bool operator!=(const ScoredMember& other) const {
    return !(*this == other);
  }
};

// ZSet - Sorted Set implementation using Abseil B-Tree
// Abseil's btree_map provides excellent performance for ordered containers
template <typename Key = std::string, typename Score = double>
class ZSet {
 public:
  using MemberType = Key;
  using ScoreType = Score;
  using ElementType = ScoredMember;
  using BTreeMap = absl::btree_map<ElementType, uint64_t>;  // Value is unused, just for ordering
  using MemberMap = absl::flat_hash_map<MemberType, ScoreType>;
  
  explicit ZSet(size_t expected_size = 1024);
  ~ZSet() = default;
  
  // Non-copyable, non-movable
  ZSet(const ZSet&) = delete;
  ZSet& operator=(const ZSet&) = delete;
  ZSet(ZSet&&) = delete;
  ZSet& operator=(ZSet&&) = delete;
  
  // Add or update a member with a score
  // Returns true if a new member was added, false if updated
  bool Add(const MemberType& member, ScoreType score);
  
  // Remove a member
  // Returns true if removed, false if not found
  bool Remove(const MemberType& member);
  
  // Get score of a member
  // Returns nullopt if member not found
  std::optional<ScoreType> GetScore(const MemberType& member) const;
  
  // Get rank of a member (0-based)
  // Returns nullopt if member not found
  // reverse = true for reverse rank (highest score = 0)
  std::optional<uint64_t> GetRank(const MemberType& member, bool reverse = false) const;
  
  // Get member by rank (0-based)
  // Returns nullopt if rank is out of range
  // reverse = true for reverse order
  std::optional<MemberType> GetByRank(uint64_t rank, bool reverse = false) const;
  
  // Get score by rank
  // Returns nullopt if rank is out of range
  std::optional<ScoreType> GetScoreByRank(uint64_t rank, bool reverse = false) const;
  
  // Get range of members by score range [min, max]
  // with_scores = true to include scores in result
  std::vector<std::pair<MemberType, ScoreType>> GetRangeByScore(
      ScoreType min, ScoreType max, bool with_scores = false) const;
  
  // Get range of members by rank [start, stop]
  std::vector<std::pair<MemberType, ScoreType>> GetRangeByRank(
      uint64_t start, uint64_t stop, bool reverse = false, bool with_scores = false) const;
  
  // Count members in score range [min, max]
  uint64_t CountRange(ScoreType min, ScoreType max) const;
  
  // Get the number of members
  size_t Size() const;
  
  // Check if empty
  bool Empty() const;
  
  // Check if member exists
  bool Contains(const MemberType& member) const;
  
  // Remove members in score range [min, max]
  uint64_t RemoveRangeByScore(ScoreType min, ScoreType max);
  
  // Clear all members
  void Clear();
  
  // Get all members (for debugging)
  std::vector<std::pair<MemberType, ScoreType>> GetAll() const;
  
 private:
  BTreeMap ordered_set_;      // Ordered by (score, member)
  MemberMap member_to_score_;  // Fast member -> score lookup
  mutable absl::Mutex mutex_;
};

// StringZSet - Specialized ZSet for string members
using StringZSet = ZSet<std::string, double>;

}  // namespace astra::container