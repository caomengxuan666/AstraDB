// ==============================================================================
// Database Interface
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <absl/random/random.h>
#include "astra/container/dash_map.hpp"
#include "astra/container/zset/btree_zset.hpp"
#include "astra/container/linked_list.hpp"
#include "astra/container/stream_data.hpp"
#include "astra/storage/key_metadata.hpp"
#include "astra/protocol/resp/resp_types.hpp"
#include "astra/core/memory/string_pool.hpp"

namespace astra::commands {

// String value (no expiration here - use metadata instead)
struct StringValue {
  std::string value;

  StringValue() = default;
  explicit StringValue(std::string v) : value(std::move(v)) {}
};

// Key-value database
class Database {
 public:
  using StringMap = astra::container::DashMap<std::string, StringValue>;
  using SetType = astra::container::DashSet<std::string>;
  using HashType = astra::container::DashMap<std::string, std::string>;
  using ZSetType = astra::container::ZSet<std::string, double>;
  using ListType = astra::container::StringList;

  Database() : string_pool_(std::make_unique<core::memory::StringPool>()) {}
  ~Database() = default;

  // Disable copy and move
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  // ========== String Pool Optimization ==========

  // Get pooled string view from pool (optimized for frequent keys)
  std::string_view GetPooledString(const std::string& str) {
    return string_pool_->AllocateString(str.data(), str.size());
  }

  // Get pooled string view from pool
  std::string_view GetPooledString(const char* data, size_t len) {
    return string_pool_->AllocateString(data, len);
  }

  // Get string pool for statistics
  core::memory::StringPool* GetStringPool() { return string_pool_.get(); }

  // ========== Stream Operations ==========

  StreamData* GetStream(const std::string& key) {
    std::shared_ptr<StreamData> stream;
    if (streams_.Get(key, &stream)) {
      return stream.get();
    }
    return nullptr;
  }

  StreamData* GetOrCreateStream(const std::string& key) {
    std::shared_ptr<StreamData> stream;
    if (streams_.Get(key, &stream)) {
      return stream.get();
    }

    // Create new stream
    stream = std::make_shared<StreamData>();
    streams_.Insert(key, stream);
    metadata_manager_.RegisterKey(key, astra::storage::KeyType::kString);  // Use String type for now
    return stream.get();
  }

  // ========== String Operations ==========

  bool Set(const std::string& key, StringValue value) {
    strings_.Insert(key, std::move(value));
    metadata_manager_.RegisterKey(key, astra::storage::KeyType::kString);
    return true;
  }

  bool Set(const std::string& key, const std::string& value) {
    StringValue str_value(value);
    return Set(key, std::move(str_value));
  }

  std::optional<StringValue> Get(const std::string& key) {
    // Check if key exists and is not expired
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    StringValue value;
    if (strings_.Get(key, &value)) {
      return value;
    }
    return std::nullopt;
  }

  // GETRANGE key start end - Get substring of string value
  std::string GetRange(const std::string& key, int64_t start, int64_t end) {
    auto value = Get(key);
    if (!value.has_value()) {
      return "";
    }
    
    const std::string& str = value->value;
    int64_t len = static_cast<int64_t>(str.size());
    
    // Handle negative indices
    if (start < 0) start = len + start;
    if (end < 0) end = len + end;
    
    // Clamp to valid range
    if (start < 0) start = 0;
    if (end >= len) end = len - 1;
    
    if (start > end || start >= len) {
      return "";
    }
    
    return str.substr(start, end - start + 1);
  }

  // SETRANGE key offset value - Overwrite part of string
  size_t SetRange(const std::string& key, int64_t offset, const std::string& value) {
    auto existing = Get(key);
    std::string str;
    
    if (existing.has_value()) {
      str = existing->value;
    }
    
    // Extend string if needed
    if (offset < 0) {
      return str.size();  // Invalid offset
    }
    
    size_t new_len = offset + value.size();
    if (new_len > str.size()) {
      str.resize(new_len, '\0');
    }
    
    // Copy value at offset
    std::copy(value.begin(), value.end(), str.begin() + offset);
    
    Set(key, str);
    return str.size();
  }

  bool Del(const std::string& key) {
    metadata_manager_.UnregisterKey(key);
    return strings_.Remove(key) || 
           hashes_.Remove(key) ||
           sets_.Remove(key) ||
           zsets_.Remove(key);
  }

  size_t Del(const std::vector<std::string>& keys) {
    std::atomic<size_t> count{0};
    
    // Use TBB parallel_for for large key sets
    if (keys.size() > 100) {
      tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
        [&](const tbb::blocked_range<size_t>& range) {
          for (size_t i = range.begin(); i != range.end(); ++i) {
            if (Del(keys[i])) {
              count.fetch_add(1, std::memory_order_relaxed);
            }
          }
        });
    } else {
      // Sequential for small key sets
      for (const auto& key : keys) {
        if (Del(key)) {
          ++count;
        }
      }
    }
    
    return count.load();
  }

  bool Exists(const std::string& key) {
    return metadata_manager_.IsValid(key);
  }

  // Get key version for WATCH support
  uint64_t GetKeyVersion(const std::string& key) {
    return metadata_manager_.GetKeyVersion(key);
  }

  std::vector<std::optional<std::string>> MGet(const std::vector<std::string>& keys) {
    std::vector<std::optional<std::string>> results(keys.size());
    
    // Use TBB parallel_for for large key sets
    if (keys.size() > 100) {
      tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
        [&](const tbb::blocked_range<size_t>& range) {
          for (size_t i = range.begin(); i != range.end(); ++i) {
            auto value = Get(keys[i]);
            if (value.has_value()) {
              results[i] = value->value;
            } else {
              results[i] = std::nullopt;
            }
          }
        });
    } else {
      // Sequential for small key sets
      for (size_t i = 0; i < keys.size(); ++i) {
        auto value = Get(keys[i]);
        if (value.has_value()) {
          results[i] = value->value;
        } else {
          results[i] = std::nullopt;
        }
      }
    }
    
    return results;
  }

  // ========== Hash Operations ==========

  bool HSet(const std::string& key, const std::string& field, const std::string& value) {
    auto hash = GetHash(key);
    if (!hash) {
      hash = std::make_shared<HashType>(16);
      hashes_.Insert(key, hash);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kHash);
    }
    return hash->Insert(field, value);
  }

  std::optional<std::string> HGet(const std::string& key, const std::string& field) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto hash = GetHash(key);
    if (!hash) {
      return std::nullopt;
    }
    std::string value;
    if (hash->Get(field, &value)) {
      return value;
    }
    return std::nullopt;
  }

  bool HDel(const std::string& key, const std::string& field) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto hash = GetHash(key);
    if (!hash) {
      return false;
    }
    return hash->Remove(field);
  }

  bool HExists(const std::string& key, const std::string& field) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto hash = GetHash(key);
    if (!hash) {
      return false;
    }
    return hash->Contains(field);
  }

  size_t HDel(const std::string& key, const std::vector<std::string>& fields) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto hash = GetHash(key);
    if (!hash) {
      return 0;
    }
    size_t count = 0;
    for (const auto& field : fields) {
      if (hash->Remove(field)) {
        ++count;
      }
    }
    return count;
  }

  std::vector<std::pair<std::string, std::string>> HGetAll(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return {};
    }
    
    auto hash = GetHash(key);
    if (!hash) {
      return {};
    }
    return hash->GetAllKeyValuePairs();
  }

  size_t HLen(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto hash = GetHash(key);
    if (!hash) {
      return 0;
    }
    return hash->Size();
  }

  // HINCRBY key field increment
  int64_t HIncrBy(const std::string& key, const std::string& field, int64_t increment) {
    auto hash = GetHash(key);
    if (!hash) {
      hash = std::make_shared<HashType>(16);
      hashes_.Insert(key, hash);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kHash);
    }
    
    std::string value;
    int64_t int_value = 0;
    if (hash->Get(field, &value)) {
      try {
        if (!absl::SimpleAtoi(value, &int_value)) {
          return 0;
        }
      } catch (...) {
        return 0;  // Will be handled by command
      }
    }
    
    int_value += increment;
    hash->Insert(field, absl::StrCat(int_value));
    return int_value;
  }

  // HINCRBYFLOAT key field increment
  double HIncrByFloat(const std::string& key, const std::string& field, double increment) {
    auto hash = GetHash(key);
    if (!hash) {
      hash = std::make_shared<HashType>(16);
      hashes_.Insert(key, hash);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kHash);
    }
    
    std::string value;
    double float_value = 0.0;
    if (hash->Get(field, &value)) {
      try {
        if (!absl::SimpleAtod(value, &float_value)) {
          return 0.0;
        }
      } catch (...) {
        return 0.0;  // Will be handled by command
      }
    }
    
    float_value += increment;
    
    // Format without trailing zeros
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(17) << float_value;
    std::string str_value = oss.str();
    
    // Remove trailing zeros
    size_t dot_pos = str_value.find('.');
    if (dot_pos != std::string::npos) {
      size_t last_non_zero = str_value.find_last_not_of('0');
      if (last_non_zero != std::string::npos && last_non_zero > dot_pos) {
        str_value = str_value.substr(0, last_non_zero + 1);
      }
      // Remove trailing dot
      if (str_value.back() == '.') {
        str_value.pop_back();
      }
    }
    
    hash->Insert(field, str_value);
    return float_value;
  }

  // HSETNX key field value - Set only if field doesn't exist
  bool HSetNx(const std::string& key, const std::string& field, const std::string& value) {
    auto hash = GetHash(key);
    if (!hash) {
      hash = std::make_shared<HashType>(16);
      hashes_.Insert(key, hash);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kHash);
      hash->Insert(field, value);
      return true;
    }
    
    // Check if field already exists
    if (hash->Contains(field)) {
      return false;
    }
    
    hash->Insert(field, value);
    return true;
  }

  // HMGET key field [field ...]
  std::vector<std::optional<std::string>> HMGet(const std::string& key, const std::vector<std::string>& fields) {
    std::vector<std::optional<std::string>> results;
    
    if (!metadata_manager_.IsValid(key)) {
      results.resize(fields.size());
      return results;
    }
    
    auto hash = GetHash(key);
    if (!hash) {
      results.resize(fields.size());
      return results;
    }
    
    for (const auto& field : fields) {
      std::string value;
      if (hash->Get(field, &value)) {
        results.emplace_back(value);
      } else {
        results.emplace_back(std::nullopt);
      }
    }
    return results;
  }

  // ========== Set Operations ==========

  bool SAdd(const std::string& key, const std::string& member) {
    auto set = GetSet(key);
    if (!set) {
      set = std::make_shared<SetType>(16);
      sets_.Insert(key, set);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kSet);
    }
    return set->Insert(member);
  }

  bool SRem(const std::string& key, const std::string& member) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto set = GetSet(key);
    if (!set) {
      return false;
    }
    return set->Remove(member);
  }

  bool SIsMember(const std::string& key, const std::string& member) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto set = GetSet(key);
    if (!set) {
      return false;
    }
    return set->Contains(member);
  }

  std::vector<std::string> SMembers(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return {};
    }
    
    auto set = GetSet(key);
    if (!set) {
      return {};
    }
    return set->GetAll();
  }

  size_t SCard(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto set = GetSet(key);
    if (!set) {
      return 0;
    }
    return set->Size();
  }

  std::optional<std::string> SPop(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto set = GetSet(key);
    if (!set || set->Empty()) {
      return std::nullopt;
    }
    
    // Get a random member and remove it
    auto members = set->GetAll();
    if (members.empty()) {
      return std::nullopt;
    }
    
    // Use absl::BitGen for random selection
    static absl::BitGen bitgen;
    size_t idx = absl::Uniform<size_t>(bitgen, 0, members.size());
    std::string member = members[idx];
    set->Remove(member);
    return member;
  }

  std::optional<std::string> SRandMember(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto set = GetSet(key);
    if (!set || set->Empty()) {
      return std::nullopt;
    }
    
    // Get a random member without removing
    auto members = set->GetAll();
    if (members.empty()) {
      return std::nullopt;
    }
    
    // Use absl::BitGen for random selection
    static absl::BitGen bitgen;
    size_t idx = absl::Uniform<size_t>(bitgen, 0, members.size());
    return members[idx];
  }

  // ========== Sorted Set Operations ==========

  bool ZAdd(const std::string& key, double score, const std::string& member) {
    auto zset = GetZSet(key);
    if (!zset) {
      zset = std::make_shared<ZSetType>(1024);
      zsets_.Insert(key, zset);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kZSet);
    }
    return zset->Add(member, score);
  }

  std::optional<double> ZScore(const std::string& key, const std::string& member) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto zset = GetZSet(key);
    if (!zset) {
      return std::nullopt;
    }
    return zset->GetScore(member);
  }

  std::vector<std::pair<std::string, double>> ZRangeByRank(
      const std::string& key, int64_t start, int64_t stop, bool reverse = false, bool with_scores = false) {
    if (!metadata_manager_.IsValid(key)) {
      return {};
    }
    
    auto zset = GetZSet(key);
    if (!zset) {
      return {};
    }
    
    uint64_t size = zset->Size();
    if (size == 0) {
      return {};
    }
    
    // Convert negative indices to positive
    if (start < 0) {
      start = static_cast<int64_t>(size) + start;
      if (start < 0) start = 0;
    }
    
    if (stop < 0) {
      stop = static_cast<int64_t>(size) + stop;
      if (stop < 0) stop = -1;
    }
    
    // Clamp to valid range
    if (start >= static_cast<int64_t>(size)) {
      return {};
    }
    
    if (stop >= static_cast<int64_t>(size)) {
      stop = static_cast<int64_t>(size) - 1;
    }
    
    return zset->GetRangeByRank(
        static_cast<uint64_t>(start),
        static_cast<uint64_t>(stop),
        false,
        with_scores
    );
  }

  bool ZRem(const std::string& key, const std::string& member) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto zset = GetZSet(key);
    if (!zset) {
      return false;
    }
    return zset->Remove(member);
  }

  // ZREVRANGE - Return a range of members in a sorted set, by score, with scores ordered from high to low
  std::vector<std::pair<std::string, double>> ZRange(
      const std::string& key, int64_t start, int64_t stop, bool reverse, bool with_scores) {
    if (!metadata_manager_.IsValid(key)) {
      return {};
    }
    
    auto zset = GetZSet(key);
    if (!zset) {
      return {};
    }
    
    uint64_t size = zset->Size();
    if (size == 0) {
      return {};
    }
    
    // Convert negative indices to positive
    if (start < 0) {
      start = static_cast<int64_t>(size) + start;
      if (start < 0) start = 0;
    }
    
    if (stop < 0) {
      stop = static_cast<int64_t>(size) + stop;
      if (stop < 0) stop = -1;
    }
    
    // Clamp to valid range
    if (start >= static_cast<int64_t>(size)) {
      return {};
    }
    
    if (stop >= static_cast<int64_t>(size)) {
      stop = static_cast<int64_t>(size) - 1;
    }
    
    return zset->GetRangeByRank(
        static_cast<uint64_t>(start),
        static_cast<uint64_t>(stop),
        reverse,
        with_scores
    );
  }

  // ZPOPMIN - Remove and return the member with the lowest score in a sorted set
  std::optional<std::pair<std::string, double>> ZPopMin(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto zset = GetZSet(key);
    if (!zset || zset->Size() == 0) {
      return std::nullopt;
    }
    
    auto result = zset->GetRangeByRank(0, 0, false, true);
    if (!result.empty()) {
      auto [member, score] = result[0];
      zset->Remove(member);
      if (zset->Size() == 0) {
        metadata_manager_.UnregisterKey(key);
      }
      return result[0];
    }
    
    return std::nullopt;
  }

  // ZPOPMAX - Remove and return the member with the highest score in a sorted set
  std::optional<std::pair<std::string, double>> ZPopMax(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto zset = GetZSet(key);
    if (!zset || zset->Size() == 0) {
      return std::nullopt;
    }
    
    auto result = zset->GetRangeByRank(zset->Size() - 1, zset->Size() - 1, false, true);
    if (!result.empty()) {
      auto [member, score] = result[0];
      zset->Remove(member);
      if (zset->Size() == 0) {
        metadata_manager_.UnregisterKey(key);
      }
      return result[0];
    }
    
    return std::nullopt;
  }

  // ZRANGEBYSCORE - Return a range of members in a sorted set, by score
  std::vector<std::pair<std::string, double>> ZRangeByScore(
      const std::string& key,
      const std::string& min_str,
      const std::string& max_str,
      bool reverse = false,
      bool with_scores = false,
      bool has_limit = false,
      int64_t offset = 0,
      int64_t count = -1) {
    if (!metadata_manager_.IsValid(key)) {
      return {};
    }
    
    auto zset = GetZSet(key);
    if (!zset) {
      return {};
    }
    
    // Parse min/max scores with possible prefixes (-inf, +inf, (, [)
    auto parse_score = [](const std::string& s) -> std::pair<bool, double> {
      if (s == "-inf" || s == "-INF") {
        return {true, -std::numeric_limits<double>::infinity()};
      }
      if (s == "+inf" || s == "+INF" || s == "inf" || s == "INF") {
        return {true, std::numeric_limits<double>::infinity()};
      }
      
      bool exclusive = false;
      std::string score_str = s;
      if (!s.empty() && (s[0] == '(' || s[0] == '[')) {
        exclusive = (s[0] == '(');
        score_str = s.substr(1);
      }
      
      double value;
      if (absl::SimpleAtod(score_str, &value)) {
        return {exclusive, value};
      }
      return {false, 0.0};
    };
    
    auto [min_exclusive, min_score] = parse_score(min_str);
    auto [max_exclusive, max_score] = parse_score(max_str);
    
    // Get range by score
    auto results = zset->GetRangeByScore(min_score, max_score, with_scores);
    
    // Filter out exclusive boundaries
    std::vector<std::pair<std::string, double>> filtered;
    for (const auto& [member, score] : results) {
      if (min_exclusive && score <= min_score) continue;
      if (max_exclusive && score >= max_score) continue;
      filtered.push_back({member, score});
    }
    
    // Reverse if needed
    if (reverse) {
      std::reverse(filtered.begin(), filtered.end());
    }
    
    // Apply LIMIT if specified
    if (has_limit) {
      std::vector<std::pair<std::string, double>> limited;
      int64_t start = offset;
      int64_t end = (count < 0) ? static_cast<int64_t>(filtered.size()) : (offset + count);
      
      if (start < 0) {
        start = static_cast<int64_t>(filtered.size()) + start;
        if (start < 0) start = 0;
      }
      
      if (start >= static_cast<int64_t>(filtered.size())) {
        return {};
      }
      
      if (end > static_cast<int64_t>(filtered.size())) {
        end = static_cast<int64_t>(filtered.size());
      }
      
      for (int64_t i = start; i < end; ++i) {
        limited.push_back(filtered[static_cast<size_t>(i)]);
      }
      
      return limited;
    }
    
    return filtered;
  }

  size_t ZCard(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto zset = GetZSet(key);
    if (!zset) {
      return 0;
    }
    return zset->Size();
  }

  uint64_t ZCount(const std::string& key, double min, double max) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto zset = GetZSet(key);
    if (!zset) {
      return 0;
    }
    return zset->CountRange(min, max);
  }

  double ZIncrBy(const std::string& key, double increment, const std::string& member) {
    auto zset = GetZSet(key);
    if (!zset) {
      zset = std::make_shared<ZSetType>(1024);
      zsets_.Insert(key, zset);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kZSet);
    }
    
    auto current_score = zset->GetScore(member);
    double new_score = current_score.value_or(0.0) + increment;
    zset->Add(member, new_score);
    return new_score;
  }

  std::optional<uint64_t> ZRank(const std::string& key, const std::string& member, bool reverse = false) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto zset = GetZSet(key);
    if (!zset) {
      return std::nullopt;
    }
    return zset->GetRank(member, reverse);
  }

  // ========== List Operations ==========

  bool LPush(const std::string& key, const std::string& value) {
    auto list = GetList(key);
    if (!list) {
      list = std::make_shared<ListType>();
      lists_.Insert(key, list);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kList);
    }
    list->PushLeft(value);
    return true;
  }

  bool RPush(const std::string& key, const std::string& value) {
    auto list = GetList(key);
    if (!list) {
      list = std::make_shared<ListType>();
      lists_.Insert(key, list);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kList);
    }
    list->PushRight(value);
    return true;
  }

  std::optional<std::string> LPop(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto list = GetList(key);
    if (!list) {
      return std::nullopt;
    }
    
    auto value = list->PopLeft();
    if (!value.has_value() || list->Empty()) {
      lists_.Remove(key);
      metadata_manager_.UnregisterKey(key);
    }
    return value;
  }

  std::optional<std::string> RPop(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto list = GetList(key);
    if (!list) {
      return std::nullopt;
    }
    
    auto value = list->PopRight();
    if (!value.has_value() || list->Empty()) {
      lists_.Remove(key);
      metadata_manager_.UnregisterKey(key);
    }
    return value;
  }

  size_t LLen(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto list = GetList(key);
    if (!list) {
      return 0;
    }
    return list->Size();
  }

  std::optional<std::string> LIndex(const std::string& key, int64_t index) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto list = GetList(key);
    if (!list) {
      return std::nullopt;
    }
    return list->Index(index);
  }

  bool LSet(const std::string& key, int64_t index, const std::string& value) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto list = GetList(key);
    if (!list) {
      return false;
    }
    return list->Set(index, value);
  }

  std::vector<std::string> LRange(const std::string& key, int64_t start, int64_t stop) {
    if (!metadata_manager_.IsValid(key)) {
      return {};
    }
    
    auto list = GetList(key);
    if (!list) {
      return {};
    }
    return list->Range(start, stop);
  }

  bool LTrim(const std::string& key, int64_t start, int64_t stop) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto list = GetList(key);
    if (!list) {
      return false;
    }
    
    list->Trim(start, stop);
    
    // Remove key if list is empty after trim
    if (list->Empty()) {
      lists_.Remove(key);
      metadata_manager_.UnregisterKey(key);
    }
    return true;
  }

  size_t LRem(const std::string& key, const std::string& value, int64_t count) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto list = GetList(key);
    if (!list) {
      return 0;
    }
    
    size_t removed = list->Remove(value, count);
    
    // Remove key if list is empty after removal
    if (list->Empty()) {
      lists_.Remove(key);
      metadata_manager_.UnregisterKey(key);
    }
    return removed;
  }

  bool LInsert(const std::string& key, int64_t pivot_index, const std::string& value, bool before = true) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto list = GetList(key);
    if (!list) {
      return false;
    }
    return list->Insert(pivot_index, value, before);
  }

  std::optional<std::string> RPopLPush(const std::string& source, const std::string& destination) {
    // Pop from source
    auto value = RPop(source);
    if (!value.has_value()) {
      return std::nullopt;
    }
    
    // Push to destination
    RPush(destination, *value);
    return value;
  }

  // ========== TTL Operations ==========

  bool SetExpireSeconds(const std::string& key, int64_t seconds) {
    return metadata_manager_.SetExpireSeconds(key, seconds);
  }

  bool SetExpireMs(const std::string& key, int64_t ms) {
    return metadata_manager_.SetExpireMs(key, ms);
  }

  int64_t GetTtlSeconds(const std::string& key) {
    return metadata_manager_.GetTtlSeconds(key);
  }

  int64_t GetTtlMs(const std::string& key) {
    return metadata_manager_.GetTtlMs(key);
  }

  bool Persist(const std::string& key) {
    return metadata_manager_.Persist(key);
  }

  std::vector<std::string> GetExpiredKeys() {
    return metadata_manager_.GetExpiredKeys();
  }

  // ========== Utility Operations ==========

  size_t Size() const {
    return strings_.Size();
  }

  // Get key type
  std::optional<astra::storage::KeyType> GetType(const std::string& key) {
    return metadata_manager_.GetKeyType(key);
  }

  // Get all keys (for KEYS command)
  std::vector<std::string> GetAllKeys() {
    return metadata_manager_.GetAllKeys();
  }

  // Get database size (number of keys)
  size_t DbSize() const {
    return metadata_manager_.Size();
  }

  // Flush all data
  void Clear() {
    strings_.Clear();
    hashes_.Clear();
    sets_.Clear();
    zsets_.Clear();
    lists_.Clear();
    metadata_manager_.Clear();
  }

 private:
  std::shared_ptr<HashType> GetHash(const std::string& key) {
    std::shared_ptr<HashType> hash;
    if (hashes_.Get(key, &hash)) {
      return hash;
    }
    return nullptr;
  }

  std::shared_ptr<SetType> GetSet(const std::string& key) {
    std::shared_ptr<SetType> set;
    if (sets_.Get(key, &set)) {
      return set;
    }
    return nullptr;
  }

  std::shared_ptr<ZSetType> GetZSet(const std::string& key) {
    std::shared_ptr<ZSetType> zset;
    if (zsets_.Get(key, &zset)) {
      return zset;
    }
    return nullptr;
  }

  std::shared_ptr<ListType> GetList(const std::string& key) {
    std::shared_ptr<ListType> list;
    if (lists_.Get(key, &list)) {
      return list;
    }
    return nullptr;
  }

  StringMap strings_;
  astra::container::DashMap<std::string, std::shared_ptr<HashType>> hashes_;
  astra::container::DashMap<std::string, std::shared_ptr<SetType>> sets_;
  astra::container::DashMap<std::string, std::shared_ptr<ZSetType>> zsets_;
  astra::container::DashMap<std::string, std::shared_ptr<ListType>> lists_;
  astra::container::DashMap<std::string, std::shared_ptr<StreamData>> streams_;
  astra::storage::KeyMetadataManager metadata_manager_;
  std::unique_ptr<core::memory::StringPool> string_pool_;
};

// Database manager - manages multiple databases
class DatabaseManager {
 public:
  DatabaseManager(size_t num_dbs = 16) : num_dbs_(num_dbs) {
    databases_.reserve(num_dbs);
    for (size_t i = 0; i < num_dbs; ++i) {
      databases_.push_back(std::make_unique<Database>());
    }
  }

  ~DatabaseManager() = default;

  Database* GetDatabase(int index) {
    if (index < 0 || static_cast<size_t>(index) >= databases_.size()) {
      return nullptr;
    }
    return databases_[index].get();
  }

  size_t GetDatabaseCount() const {
    return databases_.size();
  }

 private:
  size_t num_dbs_;
  std::vector<std::unique_ptr<Database>> databases_;
};

}  // namespace astra::commands