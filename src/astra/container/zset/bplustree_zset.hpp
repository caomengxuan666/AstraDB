// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0
// B+ Tree implementation of Sorted Set based on Dragonfly's design

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/hash/hash.h>
#include <absl/synchronization/mutex.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "astra/base/macros.hpp"
#include "astra/container/zset/bplustree.hpp"

namespace astra::container {

// ScoredMember for B+ Tree - optimized structure with reverse lookup
struct ScoredMemberCompact {
  double score;
  uint64_t member_hash;  // Hash of the member string

  ScoredMemberCompact() : score(0.0), member_hash(0) {}

  ScoredMemberCompact(double s, uint64_t h) : score(s), member_hash(h) {}

  // Comparison for ordering by score, then by hash
  bool operator<(const ScoredMemberCompact& other) const {
    if (score != other.score) {
      return score < other.score;
    }
    return member_hash < other.member_hash;
  }

  bool operator==(const ScoredMemberCompact& other) const {
    return score == other.score && member_hash == other.member_hash;
  }

  bool operator!=(const ScoredMemberCompact& other) const {
    return !(*this == other);
  }
};

// Comparator for ScoredMemberCompact with custom policy
struct ScoredMemberPolicy {
  using KeyT = ScoredMemberCompact;

  struct KeyCompareTo {
    int operator()(const ScoredMemberCompact& a,
                   const ScoredMemberCompact& b) const {
      std::less<ScoredMemberCompact> cmp;
      return cmp(a, b) ? -1 : (cmp(b, a) ? 1 : 0);
    }
  };
};

// ZSet - Sorted Set implementation using B+ Tree
// Based on Dragonfly's design with significant memory efficiency improvements
template <typename Key = std::string, typename Score = double>
class ZSetBPlus {
 public:
  using MemberType = Key;
  using ScoreType = Score;
  using ElementType = ScoredMemberCompact;
  using BPlusTreeSet = BPTree<ElementType, ScoredMemberPolicy>;
  using MemberMap = absl::flat_hash_map<MemberType, ScoreType>;

  explicit ZSetBPlus(size_t expected_size = 1024) { (void)expected_size; }
  ~ZSetBPlus() = default;

  // Non-copyable, non-movable
  ZSetBPlus(const ZSetBPlus&) = delete;
  ZSetBPlus& operator=(const ZSetBPlus&) = delete;
  ZSetBPlus(ZSetBPlus&&) = delete;
  ZSetBPlus& operator=(ZSetBPlus&&) = delete;

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
  std::optional<uint64_t> GetRank(const MemberType& member,
                                  bool reverse = false) const;

  // Get member by rank (0-based)
  // Returns nullopt if rank is out of range
  // reverse = true for reverse order
  std::optional<MemberType> GetByRank(uint64_t rank,
                                      bool reverse = false) const;

  // Get score by rank
  // Returns nullopt if rank is out of range
  std::optional<ScoreType> GetScoreByRank(uint64_t rank,
                                          bool reverse = false) const;

  // Get range of members by score range [min, max]
  // with_scores = true to include scores in result
  std::vector<std::pair<MemberType, ScoreType>> GetRangeByScore(
      ScoreType min, ScoreType max, bool with_scores = false) const;

  // Get range of members by rank [start, stop]
  std::vector<std::pair<MemberType, ScoreType>> GetRangeByRank(
      uint64_t start, uint64_t stop, bool reverse = false,
      bool with_scores = false) const;

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
  BPlusTreeSet ordered_set_;   // B+ Tree ordered by (score, hash)
  MemberMap member_to_score_;  // Fast member -> score lookup
  absl::flat_hash_map<uint64_t, MemberType>
      hash_to_member_;  // Reverse mapping: hash -> member
  mutable absl::Mutex mutex_;

  // Helper function to create ElementType from member and score
  ElementType MakeElement(const MemberType& member, ScoreType score) const {
    uint64_t hash = absl::Hash<MemberType>{}(member);
    return ElementType(score, hash);
  }

  // Helper function to find member by hash
  std::optional<MemberType> FindMemberByHash(uint64_t hash) const {
    auto it = hash_to_member_.find(hash);
    if (it != hash_to_member_.end()) {
      return it->second;
    }
    return std::nullopt;
  }
};

// StringZSetBPlus - Specialized ZSet for string members
using StringZSetBPlus = ZSetBPlus<std::string, double>;

}  // namespace astra::container
