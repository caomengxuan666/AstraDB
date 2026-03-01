// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "btree_zset.hpp"

namespace astra::container {

template <typename Key, typename Score>
ZSet<Key, Score>::ZSet(size_t expected_size) {
  member_to_score_.reserve(expected_size);
}

template <typename Key, typename Score>
bool ZSet<Key, Score>::Add(const MemberType& member, ScoreType score) {
  absl::MutexLock lock(&mutex_);
  
  auto it = member_to_score_.find(member);
  bool is_new = (it == member_to_score_.end());
  
  if (!is_new) {
    // Update existing member
    ScoreType old_score = it->second;
    
    // Remove from ordered set
    ElementType old_element(old_score, member);
    ordered_set_.erase(old_element);
    
    // Update score
    it->second = score;
  } else {
    // Add new member
    member_to_score_[member] = score;
  }
  
  // Insert into ordered set
  ElementType element(score, member);
  ordered_set_[element] = 0;  // Value is unused
  
  return is_new;
}

template <typename Key, typename Score>
bool ZSet<Key, Score>::Remove(const MemberType& member) {
  absl::MutexLock lock(&mutex_);
  
  auto it = member_to_score_.find(member);
  if (it == member_to_score_.end()) {
    return false;
  }
  
  ScoreType score = it->second;
  ElementType element(score, member);
  
  ordered_set_.erase(element);
  member_to_score_.erase(it);
  
  return true;
}

template <typename Key, typename Score>
std::optional<typename ZSet<Key, Score>::ScoreType>
ZSet<Key, Score>::GetScore(const MemberType& member) const {
  absl::ReaderMutexLock lock(&mutex_);
  
  auto it = member_to_score_.find(member);
  if (it == member_to_score_.end()) {
    return std::nullopt;
  }
  return it->second;
}

template <typename Key, typename Score>
std::optional<uint64_t> ZSet<Key, Score>::GetRank(const MemberType& member, bool reverse) const {
  absl::ReaderMutexLock lock(&mutex_);
  
  auto it = member_to_score_.find(member);
  if (it == member_to_score_.end()) {
    return std::nullopt;
  }
  
  ScoreType score = it->second;
  ElementType element(score, member);
  
  auto ordered_it = ordered_set_.find(element);
  if (ordered_it == ordered_set_.end()) {
    return std::nullopt;
  }
  
  uint64_t rank = std::distance(ordered_set_.begin(), ordered_it);
  
  if (reverse) {
    return Size() - 1 - rank;
  }
  return rank;
}

template <typename Key, typename Score>
std::optional<typename ZSet<Key, Score>::MemberType>
ZSet<Key, Score>::GetByRank(uint64_t rank, bool reverse) const {
  absl::ReaderMutexLock lock(&mutex_);
  
  uint64_t size = Size();
  if (rank >= size) {
    return std::nullopt;
  }
  
  if (reverse) {
    rank = size - 1 - rank;
  }
  
  auto it = ordered_set_.begin();
  std::advance(it, rank);
  
  return it->first.member;
}

template <typename Key, typename Score>
std::optional<typename ZSet<Key, Score>::ScoreType>
ZSet<Key, Score>::GetScoreByRank(uint64_t rank, bool reverse) const {
  absl::ReaderMutexLock lock(&mutex_);
  
  uint64_t size = Size();
  if (rank >= size) {
    return std::nullopt;
  }
  
  if (reverse) {
    rank = size - 1 - rank;
  }
  
  auto it = ordered_set_.begin();
  std::advance(it, rank);
  
  return it->first.score;
}

template <typename Key, typename Score>
std::vector<std::pair<typename ZSet<Key, Score>::MemberType, typename ZSet<Key, Score>::ScoreType>>
ZSet<Key, Score>::GetRangeByScore(ScoreType min, ScoreType max, bool with_scores) const {
  absl::ReaderMutexLock lock(&mutex_);
  
  std::vector<std::pair<MemberType, ScoreType>> result;
  
  ElementType min_element(min, "");
  ElementType max_element(max, "");
  
  // Find lower bound
  auto lower = ordered_set_.lower_bound(min_element);
  
  // Iterate until we exceed max score
  for (auto it = lower; it != ordered_set_.end(); ++it) {
    if (it->first.score > max) {
      break;
    }
    
    if (with_scores) {
      result.emplace_back(it->first.member, it->first.score);
    } else {
      result.emplace_back(it->first.member, 0.0);
    }
  }
  
  return result;
}

template <typename Key, typename Score>
std::vector<std::pair<typename ZSet<Key, Score>::MemberType, typename ZSet<Key, Score>::ScoreType>>
ZSet<Key, Score>::GetRangeByRank(uint64_t start, uint64_t stop, bool reverse, bool with_scores) const {
  absl::ReaderMutexLock lock(&mutex_);
  
  std::vector<std::pair<MemberType, ScoreType>> result;
  
  uint64_t size = Size();
  if (start >= size) {
    return result;
  }
  
  // Clamp stop to size - 1
  if (stop >= size) {
    stop = size - 1;
  }
  
  if (reverse) {
    // Swap and adjust for reverse order
    uint64_t temp = start;
    start = size - 1 - stop;
    stop = size - 1 - temp;
  }
  
  auto it_start = ordered_set_.begin();
  std::advance(it_start, start);
  
  auto it_end = ordered_set_.begin();
  std::advance(it_end, stop + 1);
  
  for (auto it = it_start; it != it_end; ++it) {
    if (with_scores) {
      result.emplace_back(it->first.member, it->first.score);
    } else {
      result.emplace_back(it->first.member, 0.0);
    }
  }
  
  if (reverse) {
    std::reverse(result.begin(), result.end());
  }
  
  return result;
}

template <typename Key, typename Score>
uint64_t ZSet<Key, Score>::CountRange(ScoreType min, ScoreType max) const {
  absl::ReaderMutexLock lock(&mutex_);
  
  ElementType min_element(min, "");
  ElementType max_element(max, "");
  
  auto lower = ordered_set_.lower_bound(min_element);
  auto upper = ordered_set_.upper_bound(max_element);
  
  return std::distance(lower, upper);
}

template <typename Key, typename Score>
size_t ZSet<Key, Score>::Size() const {
  absl::ReaderMutexLock lock(&mutex_);
  return member_to_score_.size();
}

template <typename Key, typename Score>
bool ZSet<Key, Score>::Empty() const {
  absl::ReaderMutexLock lock(&mutex_);
  return member_to_score_.empty();
}

template <typename Key, typename Score>
bool ZSet<Key, Score>::Contains(const MemberType& member) const {
  absl::ReaderMutexLock lock(&mutex_);
  return member_to_score_.contains(member);
}

template <typename Key, typename Score>
uint64_t ZSet<Key, Score>::RemoveRangeByScore(ScoreType min, ScoreType max) {
  absl::MutexLock lock(&mutex_);
  
  uint64_t count = 0;
  
  ElementType min_element(min, "");
  ElementType max_element(max, "");
  
  auto lower = ordered_set_.lower_bound(min_element);
  auto upper = ordered_set_.upper_bound(max_element);
  
  // Collect members to remove
  std::vector<MemberType> members_to_remove;
  for (auto it = lower; it != upper; ++it) {
    members_to_remove.push_back(it->first.member);
  }
  
  // Remove them
  for (const auto& member : members_to_remove) {
    if (member_to_score_.erase(member) > 0) {
      ++count;
    }
  }
  
  // Remove from ordered set
  ordered_set_.erase(lower, upper);
  
  return count;
}

template <typename Key, typename Score>
void ZSet<Key, Score>::Clear() {
  absl::MutexLock lock(&mutex_);
  
  member_to_score_.clear();
  ordered_set_.clear();
}

template <typename Key, typename Score>
std::vector<std::pair<typename ZSet<Key, Score>::MemberType, typename ZSet<Key, Score>::ScoreType>>
ZSet<Key, Score>::GetAll() const {
  absl::ReaderMutexLock lock(&mutex_);
  
  std::vector<std::pair<MemberType, ScoreType>> result;
  result.reserve(member_to_score_.size());
  
  for (const auto& [member, score] : member_to_score_) {
    result.emplace_back(member, score);
  }
  
  return result;
}

// Explicit template instantiations
template class ZSet<std::string, double>;

}  // namespace astra::container