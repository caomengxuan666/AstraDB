// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include <absl/synchronization/mutex.h>
#include "astra/container/zset/bplustree_zset.hpp"
#include <absl/synchronization/mutex.h>
#include "b_plus_tree.hpp"  // From romz-pl/b-plus-tree
#include <stdexcept>

namespace astra::container {

// Initialize B+ Tree implementation
template <typename Key, typename Score>
void ZSet<Key, Score>::InitBTree() {
  using BTreeType = bplustree::BPlusTree<ElementType, uint64_t>;
  btree_ = new BTreeType(16);  // Order 16
}

template <typename Key, typename Score>
void ZSet<Key, Score>::CleanupBTree() {
  if (btree_) {
    delete static_cast<bplustree::BPlusTree<ElementType, uint64_t>*>(btree_);
    btree_ = nullptr;
  }
}

template <typename Key, typename Score>
ZSet<Key, Score>::ZSet(size_t expected_size) : btree_(nullptr) {
  member_to_score_.reserve(expected_size);
  InitBTree();
}

template <typename Key, typename Score>
ZSet<Key, Score>::~ZSet() {
  Clear();
  CleanupBTree();
}

template <typename Key, typename Score>
bool ZSet<Key, Score>::Add(const MemberType& member, ScoreType score) {
  absl::MutexLock lock(&mutex_);
  
  auto it = member_to_score_.find(member);
  if (it != member_to_score_.end()) {
    // Update existing member
    ScoreType old_score = it->second;
    
    // Remove from B+ Tree
    ElementType old_element(old_score, member);
    auto* btree = static_cast<bplustree::BPlusTree<ElementType, uint64_t>*>(btree_);
    btree->Delete(old_element);
    
    // Update score
    it->second = score;
  } else {
    // Add new member
    member_to_score_[member] = score;
  }
  
  // Insert into B+ Tree
  ElementType element(score, member);
  auto* btree = static_cast<bplustree::BPlusTree<ElementType, uint64_t>*>(btree_);
  btree->Insert(element, 0);  // Value is not used, just ordering
  
  return (it == member_to_score_.end());
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
  
  auto* btree = static_cast<bplustree::BPlusTree<ElementType, uint64_t>*>(btree_);
  btree->Delete(element);
  
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
  
  auto* btree = static_cast<bplustree::BPlusTree<ElementType, uint64_t>*>(btree_);
  auto rank = btree->Search(element);
  
  if (!rank.has_value()) {
    return std::nullopt;
  }
  
  if (reverse) {
    return Size() - 1 - *rank;
  }
  return *rank;
}

template <typename Key, typename Score>
std::optional<typename ZSet<Key, Score>::MemberType>
ZSet<Key, Score>::GetByRank(uint64_t rank, bool reverse) const {
  absl::ReaderMutexLock lock(&mutex_);
  
  if (rank >= Size()) {
    return std::nullopt;
  }
  
  if (reverse) {
    rank = Size() - 1 - rank;
  }
  
  auto* btree = static_cast<bplustree::BPlusTree<ElementType, uint64_t>*>(btree_);
  auto element = btree->GetNth(rank);
  
  if (!element.has_value()) {
    return std::nullopt;
  }
  
  return element->member;
}

template <typename Key, typename Score>
std::optional<typename ZSet<Key, Score>::ScoreType>
ZSet<Key, Score>::GetScoreByRank(uint64_t rank, bool reverse) const {
  absl::ReaderMutexLock lock(&mutex_);
  
  if (rank >= Size()) {
    return std::nullopt;
  }
  
  if (reverse) {
    rank = Size() - 1 - rank;
  }
  
  auto* btree = static_cast<bplustree::BPlusTree<ElementType, uint64_t>*>(btree_);
  auto element = btree->GetNth(rank);
  
  if (!element.has_value()) {
    return std::nullopt;
  }
  
  return element->score;
}

template <typename Key, typename Score>
std::vector<std::pair<typename ZSet<Key, Score>::MemberType, typename ZSet<Key, Score>::ScoreType>>
ZSet<Key, Score>::GetRangeByScore(ScoreType min, ScoreType max, bool with_scores) const {
  absl::ReaderMutexLock lock(&mutex_);
  
  std::vector<std::pair<MemberType, ScoreType>> result;
  
  ElementType min_element(min, "");
  ElementType max_element(max, "");
  
  auto* btree = static_cast<bplustree::BPlusTree<ElementType, uint64_t>*>(btree_);
  
  // Search for elements in range
  auto range = btree->SearchRange(min_element, max_element);
  
  for (const auto& element : range) {
    if (with_scores) {
      result.emplace_back(element.member, element.score);
    } else {
      result.emplace_back(element.member, 0.0);
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
  
  auto* btree = static_cast<bplustree::BPlusTree<ElementType, uint64_t>*>(btree_);
  
  for (uint64_t rank = start; rank <= stop; ++rank) {
    auto element = btree->GetNth(rank);
    if (element.has_value()) {
      if (with_scores) {
        result.emplace_back(element->member, element->score);
      } else {
        result.emplace_back(element->member, 0.0);
      }
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
  
  auto* btree = static_cast<bplustree::BPlusTree<ElementType, uint64_t>*>(btree_);
  
  auto range = btree->SearchRange(min_element, max_element);
  return range.size();
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
  auto range = GetRangeByScore(min, max, false);
  
  for (const auto& [member, _] : range) {
    if (Remove(member)) {
      ++count;
    }
  }
  
  return count;
}

template <typename Key, typename Score>
void ZSet<Key, Score>::Clear() {
  absl::MutexLock lock(&mutex_);
  
  member_to_score_.clear();
  
  auto* btree = static_cast<bplustree::BPlusTree<ElementType, uint64_t>*>(btree_);
  btree->Clear();
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