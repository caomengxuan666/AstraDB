// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "astra/container/zset/bplustree_zset.hpp"

#include <absl/container/btree_set.h>
#include <absl/synchronization/mutex.h>
#include <tbb/concurrent_hash_map.h>

#include <algorithm>
#include <stdexcept>

namespace astra::container {

// Internal implementation (hidden from interface)
template <typename Key, typename Score>
struct ZSet<Key, Score>::Impl {
  using MemberScoreMap = tbb::concurrent_hash_map<Key, Score>;
  using SortedSet = absl::btree_set<ScoredMember>;

  MemberScoreMap member_to_score_;
  SortedSet sorted_set_;
  mutable absl::Mutex mutex_;  // Protects sorted_set_

  explicit Impl(size_t expected_size) {
    member_to_score_.reserve(expected_size);
  }
};

// Constructor/Destructor
template <typename Key, typename Score>
ZSet<Key, Score>::ZSet(size_t expected_size) : impl_(new Impl(expected_size)) {}

template <typename Key, typename Score>
ZSet<Key, Score>::~ZSet() {
  Clear();
  delete impl_;
}

// Add (same signature as original)
template <typename Key, typename Score>
bool ZSet<Key, Score>::Add(const MemberType& member, ScoreType score) {
  typename Impl::MemberScoreMap::accessor acc;
  bool is_new = !impl_->member_to_score_.find(acc, member);

  absl::MutexLock lock(&impl_->mutex_);

  if (!is_new) {
    ScoreType old_score = acc->second;
    ElementType old_element(old_score, member);
    impl_->sorted_set_.erase(old_element);
    acc->second = score;
  } else {
    impl_->member_to_score_.insert(acc, std::make_pair(member, score));
  }

  ElementType new_element(score, member);
  impl_->sorted_set_.insert(new_element);

  return is_new;
}

// Remove (same signature as original)
template <typename Key, typename Score>
bool ZSet<Key, Score>::Remove(const MemberType& member) {
  typename Impl::MemberScoreMap::accessor acc;
  if (!impl_->member_to_score_.find(acc, member)) {
    return false;
  }

  absl::MutexLock lock(&impl_->mutex_);

  ScoreType score = acc->second;
  ElementType element(score, member);
  impl_->sorted_set_.erase(element);
  impl_->member_to_score_.erase(acc);

  return true;
}

// GetScore (same signature as original)
template <typename Key, typename Score>
std::optional<typename ZSet<Key, Score>::ScoreType> ZSet<Key, Score>::GetScore(
    const MemberType& member) const {
  typename Impl::MemberScoreMap::const_accessor acc;
  if (!impl_->member_to_score_.find(acc, member)) {
    return std::nullopt;
  }
  return acc->second;
}

// GetRank (same signature as original)
template <typename Key, typename Score>
std::optional<uint64_t> ZSet<Key, Score>::GetRank(const MemberType& member,
                                                  bool reverse) const {
  typename Impl::MemberScoreMap::const_accessor acc;
  if (!impl_->member_to_score_.find(acc, member)) {
    return std::nullopt;
  }

  ScoreType score = acc->second;
  ElementType element(score, member);

  absl::ReaderMutexLock lock(&impl_->mutex_);

  auto it = impl_->sorted_set_.find(element);
  if (it == impl_->sorted_set_.end()) {
    return std::nullopt;
  }

  uint64_t rank = std::distance(impl_->sorted_set_.begin(), it);
  if (reverse) {
    return impl_->sorted_set_.size() - 1 - rank;
  }
  return rank;
}

// GetByRank (same signature as original)
template <typename Key, typename Score>
std::optional<typename ZSet<Key, Score>::MemberType>
ZSet<Key, Score>::GetByRank(uint64_t rank, bool reverse) const {
  absl::ReaderMutexLock lock(&impl_->mutex_);

  if (rank >= impl_->sorted_set_.size()) {
    return std::nullopt;
  }

  if (reverse) {
    rank = impl_->sorted_set_.size() - 1 - rank;
  }

  auto it = impl_->sorted_set_.begin();
  std::advance(it, rank);
  return it->member;
}

// GetScoreByRank (same signature as original)
template <typename Key, typename Score>
std::optional<typename ZSet<Key, Score>::ScoreType>
ZSet<Key, Score>::GetScoreByRank(uint64_t rank, bool reverse) const {
  absl::ReaderMutexLock lock(&impl_->mutex_);

  if (rank >= impl_->sorted_set_.size()) {
    return std::nullopt;
  }

  if (reverse) {
    rank = impl_->sorted_set_.size() - 1 - rank;
  }

  auto it = impl_->sorted_set_.begin();
  std::advance(it, rank);
  return it->score;
}

// GetRangeByScore (same signature as original)
template <typename Key, typename Score>
std::vector<std::pair<typename ZSet<Key, Score>::MemberType,
                      typename ZSet<Key, Score>::ScoreType>>
ZSet<Key, Score>::GetRangeByScore(ScoreType min, ScoreType max,
                                  bool with_scores) const {
  absl::ReaderMutexLock lock(&impl_->mutex_);

  std::vector<std::pair<MemberType, ScoreType>> result;
  ElementType min_element(min, "");
  ElementType max_element(max, "");

  auto lower = impl_->sorted_set_.lower_bound(min_element);
  auto upper = impl_->sorted_set_.upper_bound(max_element);

  for (auto it = lower; it != upper; ++it) {
    if (with_scores) {
      result.emplace_back(it->member, it->score);
    } else {
      result.emplace_back(it->member, 0.0);
    }
  }

  return result;
}

// GetRangeByRank (same signature as original)
template <typename Key, typename Score>
std::vector<std::pair<typename ZSet<Key, Score>::MemberType,
                      typename ZSet<Key, Score>::ScoreType>>
ZSet<Key, Score>::GetRangeByRank(uint64_t start, uint64_t stop, bool reverse,
                                 bool with_scores) const {
  absl::ReaderMutexLock lock(&impl_->mutex_);

  std::vector<std::pair<MemberType, ScoreType>> result;
  uint64_t size = impl_->sorted_set_.size();

  if (start >= size) {
    return result;
  }
  if (stop >= size) {
    stop = size - 1;
  }

  if (reverse) {
    uint64_t temp = start;
    start = size - 1 - stop;
    stop = size - 1 - temp;
  }

  auto it = impl_->sorted_set_.begin();
  std::advance(it, start);
  for (uint64_t i = start; i <= stop; ++i, ++it) {
    if (with_scores) {
      result.emplace_back(it->member, it->score);
    } else {
      result.emplace_back(it->member, 0.0);
    }
  }

  if (reverse) {
    std::reverse(result.begin(), result.end());
  }

  return result;
}

// CountRange (same signature as original)
template <typename Key, typename Score>
uint64_t ZSet<Key, Score>::CountRange(ScoreType min, ScoreType max) const {
  absl::ReaderMutexLock lock(&impl_->mutex_);

  ElementType min_element(min, "");
  ElementType max_element(max, "");

  auto lower = impl_->sorted_set_.lower_bound(min_element);
  auto upper = impl_->sorted_set_.upper_bound(max_element);
  return std::distance(lower, upper);
}

// Size (same signature as original)
template <typename Key, typename Score>
size_t ZSet<Key, Score>::Size() const {
  absl::ReaderMutexLock lock(&impl_->mutex_);
  return impl_->member_to_score_.size();
}

// Empty (same signature as original)
template <typename Key, typename Score>
bool ZSet<Key, Score>::Empty() const {
  absl::ReaderMutexLock lock(&impl_->mutex_);
  return impl_->member_to_score_.empty();
}

// Contains (same signature as original)
template <typename Key, typename Score>
bool ZSet<Key, Score>::Contains(const MemberType& member) const {
  typename Impl::MemberScoreMap::const_accessor acc;
  return impl_->member_to_score_.find(acc, member);
}

// RemoveRangeByScore (same signature as original)
template <typename Key, typename Score>
uint64_t ZSet<Key, Score>::RemoveRangeByScore(ScoreType min, ScoreType max) {
  auto range = GetRangeByScore(min, max, false);
  uint64_t count = 0;

  for (const auto& [member, _] : range) {
    if (Remove(member)) {
      ++count;
    }
  }

  return count;
}

// Clear (same signature as original)
template <typename Key, typename Score>
void ZSet<Key, Score>::Clear() {
  absl::MutexLock lock(&impl_->mutex_);
  impl_->member_to_score_.clear();
  impl_->sorted_set_.clear();
}

// GetAll (same signature as original)
template <typename Key, typename Score>
std::vector<std::pair<typename ZSet<Key, Score>::MemberType,
                      typename ZSet<Key, Score>::ScoreType>>
ZSet<Key, Score>::GetAll() const {
  absl::ReaderMutexLock lock(&impl_->mutex_);

  std::vector<std::pair<MemberType, ScoreType>> result;
  result.reserve(impl_->member_to_score_.size());

  for (const auto& [member, score] : impl_->member_to_score_) {
    result.emplace_back(member, score);
  }

  return result;
}

// Explicit template instantiation (same as original)
template class ZSet<std::string, double>;

}  // namespace astra::container
