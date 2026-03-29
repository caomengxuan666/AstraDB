// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/strings/str_split.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>
#include <absl/strings/numbers.h>

#include <optional>
#include <string>
#include <vector>

#include "astra/storage/key_metadata.hpp"

namespace astra::persistence {

// Data type prefixes for serialization
constexpr char kTypeString = 'S';
constexpr char kTypeHash = 'H';
constexpr char kTypeSet = 'T';
constexpr char kTypeZSet = 'Z';
constexpr char kTypeList = 'L';
constexpr char kTypeStream = 'R';

// Delimiter for serialization
constexpr char kDelimiter = '|';

// Abstract data serializer interface
class DataSerializer {
 public:
  virtual ~DataSerializer() = default;

  // Serialize data to string
  virtual std::string Serialize(const std::string& key, void* data) = 0;

  // Deserialize data from string
  virtual bool Deserialize(const std::string& serialized, void* out_data) = 0;

  // Get data type
  virtual astra::storage::KeyType GetType() const = 0;
};

// String serializer
class StringSerializer : public DataSerializer {
 public:
  std::string Serialize(const std::string& key, void* data) override {
    (void)key;  // Key is stored separately in RocksDB
    std::string* value = static_cast<std::string*>(data);
    std::string prefix(1, kTypeString);
    return prefix + kDelimiter + *value;
  }

  bool Deserialize(const std::string& serialized, void* out_data) override {
    if (serialized.empty() || serialized[0] != kTypeString) {
      return false;
    }
    std::string* out_value = static_cast<std::string*>(out_data);
    *out_value = serialized.substr(2);  // Skip prefix and delimiter
    return true;
  }

  astra::storage::KeyType GetType() const override {
    return astra::storage::KeyType::kString;
  }
};

// Hash serializer
class HashSerializer : public DataSerializer {
 public:
  std::string Serialize(const std::string& key, void* data) override {
    (void)key;
    auto* hash_map = static_cast<std::vector<std::pair<std::string, std::string>>*>(data);
    std::string result;
    result.reserve(128);
    result += kTypeHash;
    result += kDelimiter;
    for (const auto& [field, value] : *hash_map) {
      result += field + "=" + value + kDelimiter;
    }
    return result;
  }

  bool Deserialize(const std::string& serialized, void* out_data) override {
    if (serialized.empty() || serialized[0] != kTypeHash) {
      return false;
    }
    auto* out_hash = static_cast<std::vector<std::pair<std::string, std::string>>*>(out_data);
    out_hash->clear();
    
    std::string content = serialized.substr(2);  // Skip prefix and delimiter
    std::vector<std::string> pairs = absl::StrSplit(content, kDelimiter, absl::SkipEmpty());
    
    for (const auto& pair : pairs) {
      auto eq_pos = pair.find('=');
      if (eq_pos != std::string::npos) {
        out_hash->emplace_back(pair.substr(0, eq_pos), pair.substr(eq_pos + 1));
      }
    }
    return true;
  }

  astra::storage::KeyType GetType() const override {
    return astra::storage::KeyType::kHash;
  }
};

// Set serializer
class SetSerializer : public DataSerializer {
 public:
  std::string Serialize(const std::string& key, void* data) override {
    (void)key;
    auto* set = static_cast<std::vector<std::string>*>(data);
    std::string result;
    result.reserve(128);
    result += kTypeSet;
    result += kDelimiter;
    for (const auto& member : *set) {
      result += member + kDelimiter;
    }
    return result;
  }

  bool Deserialize(const std::string& serialized, void* out_data) override {
    if (serialized.empty() || serialized[0] != kTypeSet) {
      return false;
    }
    auto* out_set = static_cast<std::vector<std::string>*>(out_data);
    out_set->clear();
    
    std::string content = serialized.substr(2);  // Skip prefix and delimiter
    *out_set = absl::StrSplit(content, kDelimiter, absl::SkipEmpty());
    return true;
  }

  astra::storage::KeyType GetType() const override {
    return astra::storage::KeyType::kSet;
  }
};

// ZSet serializer
class ZSetSerializer : public DataSerializer {
 public:
  std::string Serialize(const std::string& key, void* data) override {
    (void)key;
    auto* zset = static_cast<std::vector<std::pair<std::string, double>>*>(data);
    std::string result;
    result.reserve(128);
    result += kTypeZSet;
    result += kDelimiter;
    for (const auto& [member, score] : *zset) {
      result += member + ":" + std::to_string(score) + kDelimiter;
    }
    return result;
  }

  bool Deserialize(const std::string& serialized, void* out_data) override {
    if (serialized.empty() || serialized[0] != kTypeZSet) {
      return false;
    }
    auto* out_zset = static_cast<std::vector<std::pair<std::string, double>>*>(out_data);
    out_zset->clear();
    
    std::string content = serialized.substr(2);  // Skip prefix and delimiter
    std::vector<std::string> members = absl::StrSplit(content, kDelimiter, absl::SkipEmpty());
    
    for (const auto& member_score : members) {
      auto colon_pos = member_score.find(':');
      if (colon_pos != std::string::npos) {
        std::string member = member_score.substr(0, colon_pos);
        std::string score_str = member_score.substr(colon_pos + 1);
        double score = 0.0;
        if (absl::SimpleAtod(score_str, &score)) {
          out_zset->emplace_back(member, score);
        }
      }
    }
    return true;
  }

  astra::storage::KeyType GetType() const override {
    return astra::storage::KeyType::kZSet;
  }
};

// List serializer
class ListSerializer : public DataSerializer {
 public:
  std::string Serialize(const std::string& key, void* data) override {
    (void)key;
    auto* list = static_cast<std::vector<std::string>*>(data);
    std::string result;
    result.reserve(128);
    result += kTypeList;
    result += kDelimiter;
    for (const auto& element : *list) {
      result += element + kDelimiter;
    }
    return result;
  }

  bool Deserialize(const std::string& serialized, void* out_data) override {
    if (serialized.empty() || serialized[0] != kTypeList) {
      return false;
    }
    auto* out_list = static_cast<std::vector<std::string>*>(out_data);
    out_list->clear();
    
    std::string content = serialized.substr(2);  // Skip prefix and delimiter
    *out_list = absl::StrSplit(content, kDelimiter, absl::SkipEmpty());
    return true;
  }

  astra::storage::KeyType GetType() const override {
    return astra::storage::KeyType::kList;
  }
};

// Stream serializer (placeholder for future implementation)
class StreamSerializer : public DataSerializer {
 public:
  std::string Serialize(const std::string& key, void* data) override {
    (void)key;
    (void)data;
    // TODO: Implement stream serialization
    return "";
  }

  bool Deserialize(const std::string& serialized, void* out_data) override {
    (void)serialized;
    (void)out_data;
    // TODO: Implement stream deserialization
    return false;
  }

  astra::storage::KeyType GetType() const override {
    return astra::storage::KeyType::kStream;
  }
};

// Serializer factory (Strategy Pattern)
class SerializerFactory {
 public:
  static DataSerializer* GetSerializer(astra::storage::KeyType type) {
    static StringSerializer string_serializer;
    static HashSerializer hash_serializer;
    static SetSerializer set_serializer;
    static ZSetSerializer zset_serializer;
    static ListSerializer list_serializer;
    static StreamSerializer stream_serializer;

    switch (type) {
      case astra::storage::KeyType::kString:
        return &string_serializer;
      case astra::storage::KeyType::kHash:
        return &hash_serializer;
      case astra::storage::KeyType::kSet:
        return &set_serializer;
      case astra::storage::KeyType::kZSet:
        return &zset_serializer;
      case astra::storage::KeyType::kList:
        return &list_serializer;
      case astra::storage::KeyType::kStream:
        return &stream_serializer;
      default:
        return nullptr;
    }
  }

  // Extract type from serialized data
  static std::optional<astra::storage::KeyType> ExtractType(const std::string& serialized) {
    if (serialized.empty()) {
      return std::nullopt;
    }
    char type_prefix = serialized[0];
    switch (type_prefix) {
      case kTypeString:
        return astra::storage::KeyType::kString;
      case kTypeHash:
        return astra::storage::KeyType::kHash;
      case kTypeSet:
        return astra::storage::KeyType::kSet;
      case kTypeZSet:
        return astra::storage::KeyType::kZSet;
      case kTypeList:
        return astra::storage::KeyType::kList;
      case kTypeStream:
        return astra::storage::KeyType::kStream;
      default:
        return std::nullopt;
    }
  }
};

}  // namespace astra::persistence