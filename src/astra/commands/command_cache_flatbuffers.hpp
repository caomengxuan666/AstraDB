// ==============================================================================
// Command Cache FlatBuffers Serializer
// ==============================================================================
// License: Apache 2.0
// ==============================================================================
// FlatBuffers-based serialization for command parameter caching
// Zero-copy, efficient, schema-based
// ==============================================================================

#pragma once

#include "command_cache_generated.h"
#include <absl/time/time.h>
#include <absl/time/clock.h>
#include <absl/hash/hash.h>
#include <memory>
#include <vector>
#include <string>

namespace astra::commands {

// Command parameter value wrapper
struct CachedParameterValue {
  enum class Type {
    kString = 0,
    kInteger = 1,
    kFloat = 2,
    kBoolean = 3,
    kBinary = 4,
    kArray = 5,
    kNull = 255
  };
  
  Type type;
  std::string string_value;
  int64_t int_value;
  double float_value;
  bool bool_value;
  std::vector<uint8_t> binary_data;
  std::vector<CachedParameterValue> array_values;
  bool is_null;
  
  CachedParameterValue() : type(Type::kNull), int_value(0), float_value(0.0), bool_value(false), is_null(true) {}
  
  explicit CachedParameterValue(const std::string& val) 
      : type(Type::kString), string_value(val), int_value(0), float_value(0.0), bool_value(false), is_null(false) {}
  
  explicit CachedParameterValue(int64_t val) 
      : type(Type::kInteger), int_value(val), float_value(0.0), bool_value(false), is_null(false) {}
  
  explicit CachedParameterValue(double val) 
      : type(Type::kFloat), float_value(val), int_value(0), bool_value(false), is_null(false) {}
  
  explicit CachedParameterValue(bool val) 
      : type(Type::kBoolean), bool_value(val), int_value(0), float_value(0.0), is_null(false) {}
  
  explicit CachedParameterValue(const std::vector<uint8_t>& val) 
      : type(Type::kBinary), binary_data(val), int_value(0), float_value(0.0), bool_value(false), is_null(false) {}
};

// Command cache entry wrapper
struct CachedCommandEntry {
  std::string command_name;
  std::vector<std::pair<std::string, CachedParameterValue>> parameters;
  std::string hash;
  int64_t created_at_ms;
  int64_t last_accessed_ms;
  uint32_t access_count;
  bool is_read_only;
  
  CachedCommandEntry() : created_at_ms(0), last_accessed_ms(0), access_count(0), is_read_only(false) {}
};

// Command cache FlatBuffers serializer
class CommandCacheFlatbuffersSerializer {
 public:
  // Generate hash for command
  static std::string GenerateCommandHash(const std::string& command_name,
                                         const std::vector<std::pair<std::string, CachedParameterValue>>& params) {
    std::string combined = command_name;
    for (const auto& [param_name, param_value] : params) {
      combined += "|" + param_name + ":";
      switch (param_value.type) {
        case CachedParameterValue::Type::kString:
          combined += param_value.string_value;
          break;
        case CachedParameterValue::Type::kInteger:
          combined += std::to_string(param_value.int_value);
          break;
        case CachedParameterValue::Type::kFloat:
          combined += std::to_string(param_value.float_value);
          break;
        case CachedParameterValue::Type::kBoolean:
          combined += param_value.bool_value ? "true" : "false";
          break;
        default:
          combined += "null";
          break;
      }
    }
    return std::to_string(absl::Hash<absl::string_view>{}(combined));
  }
  
  // Serialize command value
  static flatbuffers::Offset<AstraDB::Commands::CommandValue> SerializeCommandValue(
      flatbuffers::FlatBufferBuilder& builder,
      const CachedParameterValue& value) {
    
    flatbuffers::Offset<AstraDB::Commands::CommandValue> result = 0;
    
    switch (value.type) {
      case CachedParameterValue::Type::kString: {
        auto string_offset = builder.CreateString(value.string_value);
        result = AstraDB::Commands::CreateCommandValue(
          builder,
          AstraDB::Commands::DataType_String,
          string_offset,
          0,  // int_value
          0.0,  // float_value
          false,  // bool_value
          0,  // binary_data
          0,  // array_values
          false  // is_null
        );
        break;
      }
      case CachedParameterValue::Type::kInteger: {
        result = AstraDB::Commands::CreateCommandValue(
          builder,
          AstraDB::Commands::DataType_Integer,
          0,  // string_value
          value.int_value,
          0.0,  // float_value
          false,  // bool_value
          0,  // binary_data
          0,  // array_values
          false  // is_null
        );
        break;
      }
      case CachedParameterValue::Type::kFloat: {
        result = AstraDB::Commands::CreateCommandValue(
          builder,
          AstraDB::Commands::DataType_Float,
          0,  // string_value
          0,  // int_value
          value.float_value,
          false,  // bool_value
          0,  // binary_data
          0,  // array_values
          false  // is_null
        );
        break;
      }
      case CachedParameterValue::Type::kBoolean: {
        result = AstraDB::Commands::CreateCommandValue(
          builder,
          AstraDB::Commands::DataType_Boolean,
          0,  // string_value
          0,  // int_value
          0.0,  // float_value
          value.bool_value,
          0,  // binary_data
          0,  // array_values
          false  // is_null
        );
        break;
      }
      case CachedParameterValue::Type::kNull: {
        result = AstraDB::Commands::CreateCommandValue(
          builder,
          AstraDB::Commands::DataType_Null,
          0,  // string_value
          0,  // int_value
          0.0,  // float_value
          false,  // bool_value
          0,  // binary_data
          0,  // array_values
          true  // is_null
        );
        break;
      }
      default:
        // Default to null
        result = AstraDB::Commands::CreateCommandValue(
          builder,
          AstraDB::Commands::DataType_Null,
          0, 0, 0.0, false, 0, 0, true
        );
        break;
    }
    
    return result;
  }
  
  // Serialize command cache entry
  static std::vector<uint8_t> SerializeCacheEntry(const CachedCommandEntry& entry) {
    flatbuffers::FlatBufferBuilder builder;
    
    // Create parameters
    std::vector<flatbuffers::Offset<AstraDB::Commands::CommandParameter>> param_offsets;
    for (const auto& [param_name, param_value] : entry.parameters) {
      auto name_offset = builder.CreateString(param_name);
      auto value_offset = SerializeCommandValue(builder, param_value);
      
      auto param_offset = AstraDB::Commands::CreateCommandParameter(
        builder,
        name_offset,
        value_offset,
        -1  // index
      );
      param_offsets.push_back(param_offset);
    }
    auto params_offset = builder.CreateVector(param_offsets);
    
    // Create entry
    auto name_offset = builder.CreateString(entry.command_name);
    auto hash_offset = builder.CreateString(entry.hash);
    
    auto entry_offset = AstraDB::Commands::CreateCommandCacheEntry(
      builder,
      name_offset,
      params_offset,
      static_cast<uint32_t>(entry.parameters.size()),
      hash_offset,
      entry.created_at_ms,
      entry.last_accessed_ms,
      entry.access_count,
      entry.is_read_only,
      0  // estimated_size_bytes
    );
    
    builder.Finish(entry_offset);
    
    return std::vector<uint8_t>(builder.GetBufferPointer(), 
                                 builder.GetBufferPointer() + builder.GetSize());
  }
  
  // Serialize cache snapshot
  static std::vector<uint8_t> SerializeCacheSnapshot(
      const std::vector<CachedCommandEntry>& entries,
      uint64_t hit_count,
      uint64_t miss_count,
      uint32_t eviction_count) {
    flatbuffers::FlatBufferBuilder builder;
    
    auto now = absl::ToUnixMillis(absl::Now());
    
    // Create entries
    std::vector<flatbuffers::Offset<AstraDB::Commands::CommandCacheEntry>> entry_offsets;
    for (const auto& entry : entries) {
      // Serialize each entry
      auto entry_data = SerializeCacheEntry(entry);
      auto parsed_entry = AstraDB::Commands::GetCommandCacheEntry(entry_data.data());
      if (parsed_entry) {
        entry_offsets.push_back(AstraDB::Commands::CreateCommandCacheEntry(
          builder,
          parsed_entry->command_name(),
          parsed_entry->parameters(),
          parsed_entry->param_count(),
          parsed_entry->hash(),
          parsed_entry->created_at_ms(),
          parsed_entry->last_accessed_ms(),
          parsed_entry->access_count(),
          parsed_entry->is_read_only(),
          parsed_entry->estimated_size_bytes()
        ));
      }
    }
    auto entries_offset = builder.CreateVector(entry_offsets);
    
    // Create stats
    auto stats_offset = AstraDB::Commands::CreateCacheStats(
      builder,
      static_cast<uint32_t>(entries.size()),
      0,  // total_size_bytes (simplified)
      hit_count,
      miss_count,
      eviction_count,
      0,  // top_commands
      0   // top_command_counts
    );
    
    // Create snapshot
    auto version_offset = builder.CreateString("1.0.0");
    auto snapshot_offset = AstraDB::Commands::CreateCacheSnapshot(
      builder,
      entries_offset,
      stats_offset,
      now,
      version_offset
    );
    
    builder.Finish(snapshot_offset);
    
    return std::vector<uint8_t>(builder.GetBufferPointer(), 
                                 builder.GetBufferPointer() + builder.GetSize());
  }
  
  // Deserialize cache entry
  static bool DeserializeCacheEntry(const uint8_t* data, size_t size, CachedCommandEntry& out_entry) {
    if (!data || size == 0) {
      return false;
    }
    
    try {
      auto entry = AstraDB::Commands::GetCommandCacheEntry(data);
      if (!entry) {
        return false;
      }
      
      out_entry.command_name = entry->command_name()->str();
      out_entry.hash = entry->hash()->str();
      out_entry.created_at_ms = entry->created_at_ms();
      out_entry.last_accessed_ms = entry->last_accessed_ms();
      out_entry.access_count = entry->access_count();
      out_entry.is_read_only = entry->is_read_only();
      
      // Deserialize parameters
      if (entry->parameters()) {
        for (const auto* param : *entry->parameters()) {
          if (!param || !param->value()) continue;
          
          CachedParameterValue value;
          auto* fb_value = param->value();
          
          switch (fb_value->data_type()) {
            case AstraDB::Commands::DataType_String:
              value.type = CachedParameterValue::Type::kString;
              value.string_value = fb_value->string_value()->str();
              break;
            case AstraDB::Commands::DataType_Integer:
              value.type = CachedParameterValue::Type::kInteger;
              value.int_value = fb_value->int_value();
              break;
            case AstraDB::Commands::DataType_Float:
              value.type = CachedParameterValue::Type::kFloat;
              value.float_value = fb_value->float_value();
              break;
            case AstraDB::Commands::DataType_Boolean:
              value.type = CachedParameterValue::Type::kBoolean;
              value.bool_value = fb_value->bool_value();
              break;
            case AstraDB::Commands::DataType_Null:
              value.type = CachedParameterValue::Type::kNull;
              break;
            default:
              value.type = CachedParameterValue::Type::kNull;
              break;
          }
          
          out_entry.parameters.emplace_back(param->name()->str(), value);
        }
      }
      
      return true;
    } catch (...) {
      return false;
    }
  }
};

}  // namespace astra::commands
