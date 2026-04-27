// ==============================================================================
// RocksDB FlatBuffer Serializer
// ==============================================================================
// FlatBuffers-based serialization for RocksDB key-value storage
// Zero-copy, efficient, type-safe serialization
// ==============================================================================

#pragma once

#include <absl/strings/string_view.h>
#include <flatbuffers/flatbuffers.h>

#include <string>
#include <vector>

#include "generated/rocksdb_generated.h"  // Generated from rocksdb.fbs

namespace astra::persistence {

// RocksDB FlatBuffer serializer
class RocksDBSerializer {
 public:
  // Serialize string value
  static std::string SerializeString(const std::string& key,
                                     const std::string& value,
                                     int64_t timestamp = 0,
                                     int64_t ttl_ms = 0) {
    flatbuffers::FlatBufferBuilder builder;

    // Create string value
    auto string_value = AstraDB::RocksDB::CreateStringValue(
        builder,
        builder.CreateVector(reinterpret_cast<const uint8_t*>(value.data()),
                             value.size()));

    // Create key-value entry
    AstraDB::RocksDB::KeyValueBuilder kv_builder(builder);
    kv_builder.add_value_type(AstraDB::RocksDB::ValueType_String);
    kv_builder.add_timestamp(timestamp);
    kv_builder.add_ttl_ms(ttl_ms);
    kv_builder.add_string_value(string_value);

    auto kv = kv_builder.Finish();
    builder.Finish(kv);

    return std::string(
        reinterpret_cast<const char*>(builder.GetBufferPointer()),
        builder.GetSize());
  }

  // Deserialize string value
  static bool DeserializeString(const std::string& serialized,
                                std::string* out_value,
                                int64_t* out_timestamp = nullptr,
                                int64_t* out_ttl_ms = nullptr) {
    auto kv = AstraDB::RocksDB::GetKeyValue(serialized.data());
    if (!kv || kv->value_type() != AstraDB::RocksDB::ValueType_String) {
      return false;
    }

    if (kv->string_value()) {
      auto data = kv->string_value()->data();
      out_value->assign(reinterpret_cast<const char*>(data->data()),
                        data->size());
    }

    if (out_timestamp) {
      *out_timestamp = kv->timestamp();
    }

    if (out_ttl_ms) {
      *out_ttl_ms = kv->ttl_ms();
    }

    return true;
  }

  // Serialize hash value
  static std::string SerializeHash(
      const std::string& key,
      const std::vector<std::pair<std::string, std::string>>& fields,
      int64_t timestamp = 0, int64_t ttl_ms = 0) {
    flatbuffers::FlatBufferBuilder builder;

    // Create hash fields
    std::vector<flatbuffers::Offset<AstraDB::RocksDB::HashField>> field_offsets;
    for (const auto& [field, value] : fields) {
      auto field_offset = AstraDB::RocksDB::CreateHashField(
          builder, builder.CreateString(field), builder.CreateString(value));
      field_offsets.push_back(field_offset);
    }

    // Create hash value
    auto hash_value = AstraDB::RocksDB::CreateHashValue(
        builder, builder.CreateVector(field_offsets));

    // Create key-value entry
    AstraDB::RocksDB::KeyValueBuilder kv_builder(builder);
    kv_builder.add_value_type(AstraDB::RocksDB::ValueType_Hash);
    kv_builder.add_timestamp(timestamp);
    kv_builder.add_ttl_ms(ttl_ms);
    kv_builder.add_hash_value(hash_value);

    auto kv = kv_builder.Finish();
    builder.Finish(kv);

    return std::string(
        reinterpret_cast<const char*>(builder.GetBufferPointer()),
        builder.GetSize());
  }

  // Deserialize hash value
  static bool DeserializeHash(
      const std::string& serialized,
      std::vector<std::pair<std::string, std::string>>* out_fields,
      int64_t* out_timestamp = nullptr, int64_t* out_ttl_ms = nullptr) {
    auto kv = AstraDB::RocksDB::GetKeyValue(serialized.data());
    if (!kv || kv->value_type() != AstraDB::RocksDB::ValueType_Hash) {
      return false;
    }

    if (kv->hash_value()) {
      out_fields->clear();
      for (const auto* field : *kv->hash_value()->fields()) {
        out_fields->emplace_back(field->field()->str(), field->value()->str());
      }
    }

    if (out_timestamp) {
      *out_timestamp = kv->timestamp();
    }

    if (out_ttl_ms) {
      *out_ttl_ms = kv->ttl_ms();
    }

    return true;
  }

  // Serialize set value
  static std::string SerializeSet(const std::string& key,
                                  const std::vector<std::string>& members,
                                  int64_t timestamp = 0, int64_t ttl_ms = 0) {
    flatbuffers::FlatBufferBuilder builder;

    // Create set members
    std::vector<flatbuffers::Offset<flatbuffers::String>> member_offsets;
    for (const auto& member : members) {
      member_offsets.push_back(builder.CreateString(member));
    }

    // Create set value
    std::vector<flatbuffers::Offset<AstraDB::RocksDB::SetMember>> set_members;
    for (const auto& offset : member_offsets) {
      set_members.push_back(AstraDB::RocksDB::CreateSetMember(builder, offset));
    }

    auto set_value = AstraDB::RocksDB::CreateSetValue(
        builder, builder.CreateVector(set_members));

    // Create key-value entry
    AstraDB::RocksDB::KeyValueBuilder kv_builder(builder);
    kv_builder.add_value_type(AstraDB::RocksDB::ValueType_Set);
    kv_builder.add_timestamp(timestamp);
    kv_builder.add_ttl_ms(ttl_ms);
    kv_builder.add_set_value(set_value);

    auto kv = kv_builder.Finish();
    builder.Finish(kv);

    return std::string(
        reinterpret_cast<const char*>(builder.GetBufferPointer()),
        builder.GetSize());
  }

  // Deserialize set value
  static bool DeserializeSet(const std::string& serialized,
                             std::vector<std::string>* out_members,
                             int64_t* out_timestamp = nullptr,
                             int64_t* out_ttl_ms = nullptr) {
    auto kv = AstraDB::RocksDB::GetKeyValue(serialized.data());
    if (!kv || kv->value_type() != AstraDB::RocksDB::ValueType_Set) {
      return false;
    }

    if (kv->set_value()) {
      out_members->clear();
      for (const auto* member : *kv->set_value()->members()) {
        out_members->push_back(member->value()->str());
      }
    }

    if (out_timestamp) {
      *out_timestamp = kv->timestamp();
    }

    if (out_ttl_ms) {
      *out_ttl_ms = kv->ttl_ms();
    }

    return true;
  }

  // Serialize vector value
  static std::string SerializeVector(
      const std::string& key, const std::vector<float>& vector_data,
      const std::string& index_name, uint32_t dimension,
      uint8_t distance_metric, int64_t timestamp = 0, int64_t ttl_ms = 0) {
    flatbuffers::FlatBufferBuilder builder;

    auto fb_vector = builder.CreateVector(vector_data);
    auto fb_index = builder.CreateString(index_name);

    auto vector_value = AstraDB::RocksDB::CreateVectorValue(
        builder, fb_vector, dimension, distance_metric, fb_index);

    AstraDB::RocksDB::KeyValueBuilder kv_builder(builder);
    kv_builder.add_value_type(AstraDB::RocksDB::ValueType_Vector);
    kv_builder.add_timestamp(timestamp);
    kv_builder.add_ttl_ms(ttl_ms);
    kv_builder.add_vector_value(vector_value);

    auto kv = kv_builder.Finish();
    builder.Finish(kv);

    return std::string(
        reinterpret_cast<const char*>(builder.GetBufferPointer()),
        builder.GetSize());
  }

  // Deserialize vector value (fill in all output fields)
  static bool DeserializeVector(
      const std::string& serialized, std::vector<float>* out_vector,
      std::string* out_index_name, uint32_t* out_dimension,
      uint8_t* out_distance_metric, int64_t* out_timestamp = nullptr,
      int64_t* out_ttl_ms = nullptr) {
    auto kv = AstraDB::RocksDB::GetKeyValue(serialized.data());
    if (!kv || kv->value_type() != AstraDB::RocksDB::ValueType_Vector) {
      return false;
    }

    if (kv->vector_value()) {
      auto* vec_val = kv->vector_value();
      if (out_vector && vec_val->data()) {
        out_vector->assign(vec_val->data()->begin(), vec_val->data()->end());
      }
      if (out_dimension) *out_dimension = vec_val->dimension();
      if (out_distance_metric) *out_distance_metric = vec_val->distance_metric();
      if (out_index_name && vec_val->index_name()) {
        *out_index_name = vec_val->index_name()->str();
      }
    }

    if (out_timestamp) *out_timestamp = kv->timestamp();
    if (out_ttl_ms) *out_ttl_ms = kv->ttl_ms();

    return true;
  }

  // Get value type from serialized data
  static bool GetValueType(const std::string& serialized,
                           astra::storage::KeyType* out_type) {
    auto kv = AstraDB::RocksDB::GetKeyValue(serialized.data());
    if (!kv) {
      return false;
    }

    switch (kv->value_type()) {
      case AstraDB::RocksDB::ValueType_String:
        *out_type = astra::storage::KeyType::kString;
        return true;
      case AstraDB::RocksDB::ValueType_Hash:
        *out_type = astra::storage::KeyType::kHash;
        return true;
      case AstraDB::RocksDB::ValueType_Set:
        *out_type = astra::storage::KeyType::kSet;
        return true;
      case AstraDB::RocksDB::ValueType_SortedSet:
        *out_type = astra::storage::KeyType::kZSet;
        return true;
      case AstraDB::RocksDB::ValueType_List:
        *out_type = astra::storage::KeyType::kList;
        return true;
      case AstraDB::RocksDB::ValueType_Stream:
        *out_type = astra::storage::KeyType::kStream;
        return true;
      case AstraDB::RocksDB::ValueType_Vector:
        *out_type = astra::storage::KeyType::kVector;
        return true;
      case AstraDB::RocksDB::ValueType_Json:
        *out_type = astra::storage::KeyType::kJson;
        return true;
      default:
        return false;
    }
  }

  // Serialize JSON value
  static std::string SerializeJson(const std::string& key,
                                    const std::string& json_str,
                                    int64_t timestamp = 0,
                                    int64_t ttl_ms = 0) {
    flatbuffers::FlatBufferBuilder builder;
    auto fb_json = builder.CreateString(json_str);

    auto json_value = AstraDB::RocksDB::CreateJsonValue(builder, fb_json);

    AstraDB::RocksDB::KeyValueBuilder kv_builder(builder);
    kv_builder.add_value_type(AstraDB::RocksDB::ValueType_Json);
    kv_builder.add_timestamp(timestamp);
    kv_builder.add_ttl_ms(ttl_ms);
    kv_builder.add_json_value(json_value);

    auto kv = kv_builder.Finish();
    builder.Finish(kv);

    return std::string(
        reinterpret_cast<const char*>(builder.GetBufferPointer()),
        builder.GetSize());
  }

  // Deserialize JSON value
  static bool DeserializeJson(const std::string& serialized,
                               std::string* out_json,
                               int64_t* out_timestamp = nullptr,
                               int64_t* out_ttl_ms = nullptr) {
    auto kv = AstraDB::RocksDB::GetKeyValue(serialized.data());
    if (!kv || kv->value_type() != AstraDB::RocksDB::ValueType_Json) {
      return false;
    }

    if (kv->json_value() && kv->json_value()->data()) {
      *out_json = kv->json_value()->data()->str();
    }

    if (out_timestamp) *out_timestamp = kv->timestamp();
    if (out_ttl_ms) *out_ttl_ms = kv->ttl_ms();

    return true;
  }
};

}  // namespace astra::persistence
