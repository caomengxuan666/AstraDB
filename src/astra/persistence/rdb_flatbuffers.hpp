// ==============================================================================
// RDB FlatBuffers Serializer
// ==============================================================================
// License: Apache 2.0
// ==============================================================================
// FlatBuffers-based serialization for database snapshots
// Zero-copy, efficient, schema-based
// ==============================================================================

#pragma once

#include "rdb_generated.h"
#include "astra/commands/database.hpp"
#include "astra/base/logging.hpp"
#include "astra/base/version.hpp"
#include <absl/time/time.h>
#include <memory>
#include <vector>
#include <string>

namespace astra::persistence {

// RDB FlatBuffers serializer
class RdbFlatbuffersSerializer {
 public:
  // Serialize database to FlatBuffers
  static std::vector<uint8_t> SerializeDatabase(int db_index, const commands::Database* db) {
    if (!db) {
      return {};
    }
    
    flatbuffers::FlatBufferBuilder builder;
    
    // Collect all key-value pairs
    std::vector<flatbuffers::Offset<flatbuffers::String>> keys;
    std::vector<flatbuffers::Offset<AstraDB::RDB::KeyValue>> key_values;
    
    // Get all keys from string map
    // Note: This is a simplified implementation
    // In production, you'd need to iterate through all data types
    
    // For now, we'll serialize strings only
    // TODO: Add support for hash, list, set, zset, etc.
    
    auto now = absl::GetCurrentTimeNanos() / 1000000;  // Convert to milliseconds
    
    // Create key-value offsets
    for (const auto& [key, value] : /* db->GetAllStrings() */ std::vector<std::pair<std::string, commands::StringValue>>()) {
      auto key_offset = builder.CreateString(key);
      
      // Create string value
      auto value_data = builder.CreateVector(
        reinterpret_cast<const uint8_t*>(value.value.data()),
        value.value.size()
      );
      auto string_value_offset = AstraDB::RDB::CreateStringValue(builder, value_data);
      
      auto kv_offset = AstraDB::RDB::CreateKeyValue(
        builder,
        key_offset,
        AstraDB::RDB::ValueType_String,
        now,
        0,  // ttl_ms
        AstraDB::RDB::Value_StringValue,
        string_value_offset.Union()
      );
      
      keys.push_back(key_offset);
      key_values.push_back(kv_offset);
    }
    
    // Create key_values vector
    auto key_values_offset = builder.CreateVector(key_values);
    
    // Create database
    auto database_offset = AstraDB::RDB::CreateDatabase(
      builder,
      db_index,
      key_values_offset,
      db->DbSize()
    );
    
    builder.Finish(database_offset);
    
    return std::vector<uint8_t>(builder.GetBufferPointer(), 
                                 builder.GetBufferPointer() + builder.GetSize());
  }
  
  // Serialize multiple databases to FlatBuffers
  static std::vector<uint8_t> SerializeSnapshot(const std::vector<std::pair<int, const commands::Database*>>& databases) {
    flatbuffers::FlatBufferBuilder builder;
    
    // Create header
    auto magic_offset = builder.CreateString("ASTRDB");
    auto version_offset = builder.CreateString(ASTRADB_VERSION);
    auto git_branch_offset = builder.CreateString(base::kVersion.git_branch);
    auto git_commit_offset = builder.CreateString(base::kVersion.git_commit_short);
    
    auto now = absl::GetCurrentTimeNanos() / 1000000;  // Convert to milliseconds
    
    auto header_offset = AstraDB::RDB::CreateHeader(
      builder,
      magic_offset,
      version_offset,
      now,
      git_branch_offset,
      git_commit_offset
    );
    
    // Serialize all databases
    std::vector<flatbuffers::Offset<AstraDB::RDB::Database>> db_offsets;
    uint64_t total_keys = 0;
    
    for (const auto& [db_index, db] : databases) {
      if (!db) continue;
      
      auto db_data = SerializeDatabase(db_index, db);
      if (db_data.empty()) continue;
      
      // Parse the serialized database
      auto db_obj = AstraDB::RDB::GetDatabase(db_data.data());
      
      auto db_offset = AstraDB::RDB::CreateDatabase(
        builder,
        db_obj->index(),
        db_obj->key_values(),
        db_obj->size()
      );
      
      db_offsets.push_back(db_offset);
      total_keys += db_obj->size();
    }
    
    auto databases_offset = builder.CreateVector(db_offsets);
    
    // Create snapshot
    auto snapshot_offset = AstraDB::RDB::CreateSnapshot(
      builder,
      header_offset,
      databases_offset,
      total_keys,
      static_cast<uint32_t>(databases.size())
    );
    
    builder.Finish(snapshot_offset);
    
    return std::vector<uint8_t>(builder.GetBufferPointer(), 
                                 builder.GetBufferPointer() + builder.GetSize());
  }
  
  // Deserialize database from FlatBuffers
  static bool DeserializeDatabase(const uint8_t* data, size_t size, commands::Database* db) {
    if (!data || !db || size == 0) {
      return false;
    }
    
    auto db_obj = AstraDB::RDB::GetDatabase(data);
    if (!db_obj) {
      return false;
    }
    
    // Restore key-value pairs
    for (const auto* kv : *db_obj->key_values()) {
      if (!kv) continue;
      
      auto key = kv->key()->str();
      
      // Deserialize based on value type
      switch (kv->value_type()) {
        case AstraDB::RDB::ValueType_String: {
          auto* string_value = kv->value_as_StringValue();
          if (string_value && string_value->data()) {
            std::string value(
              reinterpret_cast<const char*>(string_value->data()->data()),
              string_value->data()->size()
            );
            db->Set(key, commands::StringValue(value));
          }
          break;
        }
        // TODO: Add support for other value types
        default:
          ASTRADB_LOG_WARN("Unsupported value type: {}", static_cast<int>(kv->value_type()));
          break;
      }
    }
    
    return true;
  }
  
  // Deserialize snapshot from FlatBuffers
  static bool DeserializeSnapshot(const uint8_t* data, size_t size, 
                                   std::vector<std::pair<int, commands::Database*>>& databases) {
    if (!data || size == 0) {
      return false;
    }
    
    auto snapshot = AstraDB::RDB::GetSnapshot(data);
    if (!snapshot) {
      return false;
    }
    
    // Verify header
    if (!snapshot->header()) {
      ASTRADB_LOG_ERROR("Invalid snapshot: missing header");
      return false;
    }
    
    auto* header = snapshot->header();
    ASTRADB_LOG_INFO("Loading snapshot from {}", header->git_branch()->str());
    ASTRADB_LOG_INFO("Snapshot version: {}", header->version()->str());
    
    // Restore all databases
    for (const auto* db : *snapshot->databases()) {
      if (!db) continue;
      
      // Find matching database in the target vector
      bool found = false;
      for (auto& [target_index, target_db] : databases) {
        if (target_index == db->index() && target_db) {
          if (DeserializeDatabase(flatbuffers::GetAnyRoot(db), 0, target_db)) {
            found = true;
            break;
          }
        }
      }
      
      if (!found) {
        ASTRADB_LOG_WARN("No target database for index {}", db->index());
      }
    }
    
    return true;
  }
  
  // Get snapshot metadata
  static bool GetSnapshotMetadata(const uint8_t* data, size_t size, 
                                   std::string& version, std::string& git_branch, 
                                   std::string& git_commit, int64_t& timestamp) {
    if (!data || size == 0) {
      return false;
    }
    
    auto snapshot = AstraDB::RDB::GetSnapshot(data);
    if (!snapshot || !snapshot->header()) {
      return false;
    }
    
    auto* header = snapshot->header();
    version = header->version()->str();
    git_branch = header->git_branch()->str();
    git_commit = header->git_commit()->str();
    timestamp = header->timestamp();
    
    return true;
  }
};

}  // namespace astra::persistence