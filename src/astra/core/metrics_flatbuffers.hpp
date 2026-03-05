// ==============================================================================
// Metrics FlatBuffers Serializer
// ==============================================================================
// License: Apache 2.0
// ==============================================================================
// FlatBuffers-based serialization for metrics export
// Zero-copy, efficient, schema-based
// ==============================================================================

#pragma once

#include "metrics_generated.h"
#include <absl/time/time.h>
#include <absl/time/clock.h>
#include <memory>
#include <vector>
#include <string>

namespace astra::metrics {

// Metrics FlatBuffers serializer
class MetricsFlatbuffersSerializer {
 public:
  // Serialize counter metric
  static std::vector<uint8_t> SerializeCounterMetric(
      const std::string& name,
      double value,
      const std::string& help,
      const std::vector<std::pair<std::string, std::string>>& labels) {
    flatbuffers::FlatBufferBuilder builder;
    
    auto now = absl::ToUnixMillis(absl::Now());
    
    // Create labels
    std::vector<flatbuffers::Offset<AstraDB::Metrics::MetricLabel>> label_offsets;
    for (const auto& [label_name, label_value] : labels) {
      auto name_offset = builder.CreateString(label_name);
      auto value_offset = builder.CreateString(label_value);
      auto label_offset = AstraDB::Metrics::CreateMetricLabel(builder, name_offset, value_offset);
      label_offsets.push_back(label_offset);
    }
    auto labels_offset = builder.CreateVector(label_offsets);
    
    // Create counter metric
    auto name_offset = builder.CreateString(name);
    auto help_offset = builder.CreateString(help);
    auto counter_offset = AstraDB::Metrics::CreateCounterMetric(
      builder,
      name_offset,
      value,
      labels_offset,
      help_offset
    );
    
    // Create metric wrapper
    auto metric_offset = AstraDB::Metrics::CreateMetric(
      builder,
      AstraDB::Metrics::MetricType_Counter,
      counter_offset,
      0,  // gauge
      0,  // histogram
      0,  // summary
      now
    );
    
    builder.Finish(metric_offset);
    
    return std::vector<uint8_t>(builder.GetBufferPointer(), 
                                 builder.GetBufferPointer() + builder.GetSize());
  }
  
  // Serialize gauge metric
  static std::vector<uint8_t> SerializeGaugeMetric(
      const std::string& name,
      double value,
      const std::string& help,
      const std::vector<std::pair<std::string, std::string>>& labels) {
    flatbuffers::FlatBufferBuilder builder;
    
    auto now = absl::ToUnixMillis(absl::Now());
    
    // Create labels
    std::vector<flatbuffers::Offset<AstraDB::Metrics::MetricLabel>> label_offsets;
    for (const auto& [label_name, label_value] : labels) {
      auto name_offset = builder.CreateString(label_name);
      auto value_offset = builder.CreateString(label_value);
      auto label_offset = AstraDB::Metrics::CreateMetricLabel(builder, name_offset, value_offset);
      label_offsets.push_back(label_offset);
    }
    auto labels_offset = builder.CreateVector(label_offsets);
    
    // Create gauge metric
    auto name_offset = builder.CreateString(name);
    auto help_offset = builder.CreateString(help);
    auto gauge_offset = AstraDB::Metrics::CreateGaugeMetric(
      builder,
      name_offset,
      value,
      labels_offset,
      help_offset
    );
    
    // Create metric wrapper
    auto metric_offset = AstraDB::Metrics::CreateMetric(
      builder,
      AstraDB::Metrics::MetricType_Gauge,
      0,  // counter
      gauge_offset,
      0,  // histogram
      0,  // summary
      now
    );
    
    builder.Finish(metric_offset);
    
    return std::vector<uint8_t>(builder.GetBufferPointer(), 
                                 builder.GetBufferPointer() + builder.GetSize());
  }
  
  // Serialize metrics batch
  static std::vector<uint8_t> SerializeMetricsBatch(
      const std::vector<std::vector<uint8_t>>& metrics_data,
      const std::string& host = "localhost",
      const std::string& instance = "astradb",
      const std::string& job = "astradb") {
    flatbuffers::FlatBufferBuilder builder;
    
    auto now = absl::ToUnixMillis(absl::Now());
    
    // Parse and collect metrics
    std::vector<flatbuffers::Offset<AstraDB::Metrics::Metric>> metric_offsets;
    for (const auto& metric_data : metrics_data) {
      if (!metric_data.empty()) {
        auto metric = AstraDB::Metrics::GetMetric(metric_data.data());
        if (metric) {
          metric_offsets.push_back(AstraDB::Metrics::CreateMetric(
            builder,
            metric->metric_type(),
            metric->counter(),
            metric->gauge(),
            metric->histogram(),
            metric->summary(),
            metric->timestamp_ms()
          ));
        }
      }
    }
    auto metrics_offset = builder.CreateVector(metric_offsets);
    
    // Create batch
    auto host_offset = builder.CreateString(host);
    auto instance_offset = builder.CreateString(instance);
    auto job_offset = builder.CreateString(job);
    
    auto batch_offset = AstraDB::Metrics::CreateMetricsBatch(
      builder,
      metrics_offset,
      now,
      host_offset,
      instance_offset,
      job_offset
    );
    
    builder.Finish(batch_offset);
    
    return std::vector<uint8_t>(builder.GetBufferPointer(), 
                                 builder.GetBufferPointer() + builder.GetSize());
  }
  
  // Get batch size
  static size_t GetBatchSize(const uint8_t* data) {
    if (!data) return 0;
    auto batch = AstraDB::Metrics::GetMetricsBatch(data);
    if (!batch) return 0;
    return batch->metrics()->size();
  }
};

}  // namespace astra::metrics
