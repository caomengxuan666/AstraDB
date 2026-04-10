// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "astra/container/zset/bplustree_zset.hpp"

#include <algorithm>

namespace astra::container {

template <typename Key, typename Score>
bool ZSetBPlus<Key, Score>::Add(const MemberType& member, ScoreType score) {
  absl::MutexLock lock(&mutex_);

  auto it = member_to_score_.find(member);
  if (it != member_to_score_.end()) {
    // Update existing member
    ScoreType old_score = it->second;
    if (old_score != score) {
      // Remove old element and insert new one
      ElementType old_elem = MakeElement(member, old_score);
      ordered_set_.Delete(old_elem);

      ElementType new_elem = MakeElement(member, score);
      ordered_set_.Insert(new_elem);

      it->second = score;
      // Update reverse mapping
      uint64_t hash = absl::Hash<MemberType>{}(member);
      hash_to_member_[hash] = member;
    }
    return false;  // Updated, not added
  } else {
    // Add new member
    ElementType elem = MakeElement(member, score);
    ordered_set_.Insert(elem);
    member_to_score_[member] = score;
    // Add to reverse mapping
    uint64_t hash = absl::Hash<MemberType>{}(member);
    hash_to_member_[hash] = member;
    return true;  // Added
  }
}

template <typename Key, typename Score>
bool ZSetBPlus<Key, Score>::Remove(const MemberType& member) {
  absl::MutexLock lock(&mutex_);

  auto it = member_to_score_.find(member);
  if (it != member_to_score_.end()) {
    ScoreType score = it->second;
    ElementType elem = MakeElement(member, score);
    ordered_set_.Delete(elem);
    member_to_score_.erase(it);
    // Remove from reverse mapping
    uint64_t hash = absl::Hash<MemberType>{}(member);
    hash_to_member_.erase(hash);
    return true;  // Removed
  }
  return false;  // Not found
}

template <typename Key, typename Score>
std::optional<typename ZSetBPlus<Key, Score>::ScoreType>
ZSetBPlus<Key, Score>::GetScore(const MemberType& member) const {
  absl::MutexLock lock(&mutex_);

  auto it = member_to_score_.find(member);
  if (it != member_to_score_.end()) {
    return it->second;
  }
  return std::nullopt;
}

template <typename Key, typename Score>
std::optional<uint64_t> ZSetBPlus<Key, Score>::GetRank(const MemberType& member,
                                                       bool reverse) const {
  absl::MutexLock lock(&mutex_);

  auto it = member_to_score_.find(member);
  if (it == member_to_score_.end()) {
    return std::nullopt;
  }

  ElementType elem = MakeElement(member, it->second);
  std::optional<uint32_t> rank_opt = ordered_set_.GetRank(elem, reverse);

  if (!rank_opt.has_value()) {
    return std::nullopt;
  }

  return static_cast<uint64_t>(rank_opt.value());
}

template <typename Key, typename Score>
std::optional<typename ZSetBPlus<Key, Score>::MemberType>
ZSetBPlus<Key, Score>::GetByRank(uint64_t rank, bool reverse) const {
  absl::MutexLock lock(&mutex_);

  if (rank >= ordered_set_.Size()) {
    return std::nullopt;
  }

  auto path = ordered_set_.FromRank(static_cast<uint32_t>(rank));
  if (path.Depth() == 0) {
    return std::nullopt;
  }

  // Use reverse mapping to quickly find the member
  auto [node, pos] = path.Last();
  ElementType elem = node->Key(pos);
  return FindMemberByHash(elem.member_hash);
}

template <typename Key, typename Score>
std::optional<typename ZSetBPlus<Key, Score>::ScoreType>
ZSetBPlus<Key, Score>::GetScoreByRank(uint64_t rank, bool reverse) const {
  absl::MutexLock lock(&mutex_);

  if (rank >= ordered_set_.Size()) {
    return std::nullopt;
  }

  auto path = ordered_set_.FromRank(static_cast<uint32_t>(rank));
  if (path.Depth() == 0) {
    return std::nullopt;
  }

  auto [node, pos] = path.Last();
  ElementType elem = node->Key(pos);
  return elem.score;
}

template <typename Key, typename Score>
std::vector<std::pair<typename ZSetBPlus<Key, Score>::MemberType,
                      typename ZSetBPlus<Key, Score>::ScoreType>>
ZSetBPlus<Key, Score>::GetRangeByScore(ScoreType min, ScoreType max,
                                       bool with_scores) const {
  absl::MutexLock lock(&mutex_);

  std::vector<std::pair<MemberType, ScoreType>> result;

  // Iterate through all elements and filter by score range
  for (uint32_t i = 0; i < ordered_set_.Size(); ++i) {
    auto path = ordered_set_.FromRank(i);
    if (path.Depth() == 0) continue;

    auto [node, pos] = path.Last();
    ElementType elem = node->Key(pos);
    double score = elem.score;

    if (score >= min && score <= max) {
      // Use reverse mapping to quickly find the member
      auto member_opt = FindMemberByHash(elem.member_hash);
      if (member_opt.has_value()) {
        if (with_scores) {
          result.push_back({member_opt.value(), score});
        } else {
          result.push_back({member_opt.value(), 0.0});
        }
      }
    }
  }

  return result;
}

template <typename Key, typename Score>
std::vector<std::pair<typename ZSetBPlus<Key, Score>::MemberType,
                      typename ZSetBPlus<Key, Score>::ScoreType>>
ZSetBPlus<Key, Score>::GetRangeByRank(uint64_t start, uint64_t stop,
                                      bool reverse, bool with_scores) const {
  absl::MutexLock lock(&mutex_);

  std::vector<std::pair<MemberType, ScoreType>> result;

  if (start >= ordered_set_.Size()) {
    return result;
  }

  stop = std::min(stop, ordered_set_.Size() - 1);

  for (uint64_t i = start; i <= stop; ++i) {
    uint32_t rank = reverse ? (ordered_set_.Size() - 1 - i) : i;
    auto path = ordered_set_.FromRank(rank);
    if (path.Depth() == 0) continue;

    auto [node, pos] = path.Last();
    ElementType elem = node->Key(pos);
    double score = elem.score;

    // Use reverse mapping to quickly find the member
    auto member_opt = FindMemberByHash(elem.member_hash);
    if (member_opt.has_value()) {
      if (with_scores) {
        result.push_back({member_opt.value(), score});
      } else {
        result.push_back({member_opt.value(), 0.0});
      }
    }
  }

  return result;
}

template <typename Key, typename Score>
uint64_t ZSetBPlus<Key, Score>::CountRange(ScoreType min, ScoreType max) const {
  absl::MutexLock lock(&mutex_);

  uint64_t count = 0;

  for (uint32_t i = 0; i < ordered_set_.Size(); ++i) {
    auto path = ordered_set_.FromRank(i);
    if (path.Depth() == 0) continue;

    auto [node, pos] = path.Last();
    ElementType elem = node->Key(pos);
    double score = elem.score;

    if (score >= min && score <= max) {
      count++;
    }
  }

  return count;
}

template <typename Key, typename Score>
size_t ZSetBPlus<Key, Score>::Size() const {
  absl::MutexLock lock(&mutex_);
  return ordered_set_.Size();
}

template <typename Key, typename Score>
bool ZSetBPlus<Key, Score>::Empty() const {
  absl::MutexLock lock(&mutex_);
  return ordered_set_.Empty();
}

template <typename Key, typename Score>
bool ZSetBPlus<Key, Score>::Contains(const MemberType& member) const {
  absl::MutexLock lock(&mutex_);
  return member_to_score_.find(member) != member_to_score_.end();
}

template <typename Key, typename Score>
uint64_t ZSetBPlus<Key, Score>::RemoveRangeByScore(ScoreType min,
                                                   ScoreType max) {
  absl::MutexLock lock(&mutex_);

  uint64_t removed = 0;
  std::vector<MemberType> to_remove;

  // Find all members in the score range
  for (const auto& [member, score] : member_to_score_) {
    if (score >= min && score <= max) {
      to_remove.push_back(member);
    }
  }

  // Remove them
  for (const auto& member : to_remove) {
    if (Remove(member)) {
      removed++;
    }
  }

  return removed;
}

template <typename Key, typename Score>
void ZSetBPlus<Key, Score>::Clear() {
  absl::MutexLock lock(&mutex_);
  ordered_set_.Clear();
  member_to_score_.clear();
  hash_to_member_.clear();
}

template <typename Key, typename Score>
std::vector<std::pair<typename ZSetBPlus<Key, Score>::MemberType,
                      typename ZSetBPlus<Key, Score>::ScoreType>>
ZSetBPlus<Key, Score>::GetAll() const {
  absl::MutexLock lock(&mutex_);

  std::vector<std::pair<MemberType, ScoreType>> result;
  result.reserve(member_to_score_.size());

  for (const auto& [member, score] : member_to_score_) {
    result.push_back({member, score});
  }

  return result;
}

// Explicit template instantiations
template class ZSetBPlus<std::string, double>;

}  // namespace astra::container
