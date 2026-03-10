#pragma once

#include <absl/time/time.h>

#include <cstdint>
#include <string>
#include <vector>

#include "metrics_generated.h"

namespace astra {
namespace core {

class MetricsSerializer {
 public:
  // Serialize a single counter metric
  static std::vector<uint8_t> SerializeCounterMetric(
      const std::string& name, double value,
      const std::vector<std::pair<std::string, std::string>>& labels = {},
      const std::string& help = "") {
    flatbuffers::FlatBufferBuilder builder;

    // Create label offsets
    std::vector<flatbuffers::Offset<AstraDB::Metrics::MetricLabel>>
        label_offsets;
    for (const auto& [label_name, label_value] : labels) {
      auto name_offset = builder.CreateString(label_name);
      auto value_offset = builder.CreateString(label_value);
      label_offsets.push_back(AstraDB::Metrics::CreateMetricLabel(
          builder, name_offset, value_offset));
    }

    auto labels_offset = builder.CreateVector(label_offsets);
    auto name_offset = builder.CreateString(name);
    auto help_offset = builder.CreateString(help);

    auto counter_offset = AstraDB::Metrics::CreateCounterMetric(
        builder, name_offset, value, labels_offset, help_offset);

    auto metric_offset = AstraDB::Metrics::CreateMetric(
        builder, AstraDB::Metrics::MetricType_Counter, counter_offset,
        0,  // gauge
        0,  // histogram
        0,  // summary
        absl::ToUnixMillis(absl::Now()));

    builder.Finish(metric_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Serialize a single gauge metric
  static std::vector<uint8_t> SerializeGaugeMetric(
      const std::string& name, double value,
      const std::vector<std::pair<std::string, std::string>>& labels = {},
      const std::string& help = "") {
    flatbuffers::FlatBufferBuilder builder;

    // Create label offsets
    std::vector<flatbuffers::Offset<AstraDB::Metrics::MetricLabel>>
        label_offsets;
    for (const auto& [label_name, label_value] : labels) {
      auto name_offset = builder.CreateString(label_name);
      auto value_offset = builder.CreateString(label_value);
      label_offsets.push_back(AstraDB::Metrics::CreateMetricLabel(
          builder, name_offset, value_offset));
    }

    auto labels_offset = builder.CreateVector(label_offsets);
    auto name_offset = builder.CreateString(name);
    auto help_offset = builder.CreateString(help);

    auto gauge_offset = AstraDB::Metrics::CreateGaugeMetric(
        builder, name_offset, value, labels_offset, help_offset);

    auto metric_offset = AstraDB::Metrics::CreateMetric(
        builder, AstraDB::Metrics::MetricType_Gauge,
        0,  // counter
        gauge_offset,
        0,  // histogram
        0,  // summary
        absl::ToUnixMillis(absl::Now()));

    builder.Finish(metric_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Serialize metrics batch - simplified version that just passes through the
  // raw data
  static std::vector<uint8_t> SerializeMetricsBatch(
      const std::vector<std::vector<uint8_t>>& metrics_data,
      const std::string& host = "localhost",
      const std::string& instance = "astradb",
      const std::string& job = "astradb") {
    flatbuffers::FlatBufferBuilder builder;

    auto now = absl::ToUnixMillis(absl::Now());

    // Create metric offsets by creating new metrics from the parsed data
    std::vector<flatbuffers::Offset<AstraDB::Metrics::Metric>> metric_offsets;
    for (const auto& metric_data : metrics_data) {
      if (!metric_data.empty()) {
        auto metric =
            flatbuffers::GetRoot<AstraDB::Metrics::Metric>(metric_data.data());
        if (metric) {
          // Create new metric by copying data
          AstraDB::Metrics::MetricType metric_type = metric->metric_type();
          flatbuffers::Offset<AstraDB::Metrics::CounterMetric> counter_offset =
              0;
          flatbuffers::Offset<AstraDB::Metrics::GaugeMetric> gauge_offset = 0;

          if (metric_type == AstraDB::Metrics::MetricType_Counter &&
              metric->counter()) {
            // Re-create counter metric
            auto counter = metric->counter();
            auto name_offset = builder.CreateString(
                counter->name() ? counter->name()->str() : "");
            auto help_offset = builder.CreateString(
                counter->help() ? counter->help()->str() : "");

            // Re-create labels
            std::vector<flatbuffers::Offset<AstraDB::Metrics::MetricLabel>>
                label_offsets;
            if (counter->labels()) {
              for (unsigned i = 0; i < counter->labels()->size(); i++) {
                auto label = counter->labels()->Get(i);
                auto label_name = builder.CreateString(
                    label->name() ? label->name()->str() : "");
                auto label_value = builder.CreateString(
                    label->value() ? label->value()->str() : "");
                label_offsets.push_back(AstraDB::Metrics::CreateMetricLabel(
                    builder, label_name, label_value));
              }
            }
            auto labels_offset = builder.CreateVector(label_offsets);

            counter_offset = AstraDB::Metrics::CreateCounterMetric(
                builder, name_offset, counter->value(), labels_offset,
                help_offset);
          } else if (metric_type == AstraDB::Metrics::MetricType_Gauge &&
                     metric->gauge()) {
            // Re-create gauge metric
            auto gauge = metric->gauge();
            auto name_offset =
                builder.CreateString(gauge->name() ? gauge->name()->str() : "");
            auto help_offset =
                builder.CreateString(gauge->help() ? gauge->help()->str() : "");

            // Re-create labels
            std::vector<flatbuffers::Offset<AstraDB::Metrics::MetricLabel>>
                label_offsets;
            if (gauge->labels()) {
              for (unsigned i = 0; i < gauge->labels()->size(); i++) {
                auto label = gauge->labels()->Get(i);
                auto label_name = builder.CreateString(
                    label->name() ? label->name()->str() : "");
                auto label_value = builder.CreateString(
                    label->value() ? label->value()->str() : "");
                label_offsets.push_back(AstraDB::Metrics::CreateMetricLabel(
                    builder, label_name, label_value));
              }
            }
            auto labels_offset = builder.CreateVector(label_offsets);

            gauge_offset = AstraDB::Metrics::CreateGaugeMetric(
                builder, name_offset, gauge->value(), labels_offset,
                help_offset);
          }

          if (counter_offset.o != 0 || gauge_offset.o != 0) {
            metric_offsets.push_back(AstraDB::Metrics::CreateMetric(
                builder, metric_type, counter_offset, gauge_offset,
                0,  // histogram
                0,  // summary
                metric->timestamp_ms()));
          }
        }
      }
    }

    auto metrics_vector = builder.CreateVector(metric_offsets);
    auto host_offset = builder.CreateString(host);
    auto instance_offset = builder.CreateString(instance);
    auto job_offset = builder.CreateString(job);

    auto batch_offset = AstraDB::Metrics::CreateMetricsBatch(
        builder, metrics_vector, now, host_offset, instance_offset, job_offset);

    builder.Finish(batch_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Deserialize metrics batch
  static bool DeserializeMetricsBatch(
      const std::vector<uint8_t>& data,
      std::vector<std::vector<uint8_t>>& metrics_output) {
    if (data.empty()) return false;

    auto batch =
        flatbuffers::GetRoot<AstraDB::Metrics::MetricsBatch>(data.data());
    if (!batch || !batch->metrics()) return false;

    metrics_output.clear();
    for (unsigned i = 0; i < batch->metrics()->size(); i++) {
      auto metric = batch->metrics()->Get(i);

      // Re-serialize each metric to create a standalone buffer
      flatbuffers::FlatBufferBuilder builder;
      flatbuffers::Offset<AstraDB::Metrics::CounterMetric> counter_offset = 0;
      flatbuffers::Offset<AstraDB::Metrics::GaugeMetric> gauge_offset = 0;

      if (metric->metric_type() == AstraDB::Metrics::MetricType_Counter &&
          metric->counter()) {
        // Re-create counter
        auto counter = metric->counter();
        auto name_offset =
            builder.CreateString(counter->name() ? counter->name()->str() : "");
        auto help_offset =
            builder.CreateString(counter->help() ? counter->help()->str() : "");

        std::vector<flatbuffers::Offset<AstraDB::Metrics::MetricLabel>>
            label_offsets;
        if (counter->labels()) {
          for (unsigned j = 0; j < counter->labels()->size(); j++) {
            auto label = counter->labels()->Get(j);
            auto label_name =
                builder.CreateString(label->name() ? label->name()->str() : "");
            auto label_value = builder.CreateString(
                label->value() ? label->value()->str() : "");
            label_offsets.push_back(AstraDB::Metrics::CreateMetricLabel(
                builder, label_name, label_value));
          }
        }
        auto labels_offset = builder.CreateVector(label_offsets);

        counter_offset = AstraDB::Metrics::CreateCounterMetric(
            builder, name_offset, counter->value(), labels_offset, help_offset);
      } else if (metric->metric_type() == AstraDB::Metrics::MetricType_Gauge &&
                 metric->gauge()) {
        // Re-create gauge
        auto gauge = metric->gauge();
        auto name_offset =
            builder.CreateString(gauge->name() ? gauge->name()->str() : "");
        auto help_offset =
            builder.CreateString(gauge->help() ? gauge->help()->str() : "");

        std::vector<flatbuffers::Offset<AstraDB::Metrics::MetricLabel>>
            label_offsets;
        if (gauge->labels()) {
          for (unsigned j = 0; j < gauge->labels()->size(); j++) {
            auto label = gauge->labels()->Get(j);
            auto label_name =
                builder.CreateString(label->name() ? label->name()->str() : "");
            auto label_value = builder.CreateString(
                label->value() ? label->value()->str() : "");
            label_offsets.push_back(AstraDB::Metrics::CreateMetricLabel(
                builder, label_name, label_value));
          }
        }
        auto labels_offset = builder.CreateVector(label_offsets);

        gauge_offset = AstraDB::Metrics::CreateGaugeMetric(
            builder, name_offset, gauge->value(), labels_offset, help_offset);
      }

      if (counter_offset.o != 0 || gauge_offset.o != 0) {
        auto metric_offset = AstraDB::Metrics::CreateMetric(
            builder, metric->metric_type(), counter_offset, gauge_offset,
            0,  // histogram
            0,  // summary
            metric->timestamp_ms());
        builder.Finish(metric_offset);

        metrics_output.emplace_back(
            builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize());
      }
    }

    return true;
  }

  // Deserialize a single metric
  static bool DeserializeMetric(const std::vector<uint8_t>& data,
                                std::string& name_out, double& value_out,
                                AstraDB::Metrics::MetricType& type_out) {
    if (data.empty()) return false;

    auto metric = flatbuffers::GetRoot<AstraDB::Metrics::Metric>(data.data());
    if (!metric) return false;

    type_out = metric->metric_type();

    if (type_out == AstraDB::Metrics::MetricType_Counter && metric->counter()) {
      auto counter = metric->counter();
      name_out = counter->name() ? counter->name()->str() : "";
      value_out = counter->value();
      return true;
    } else if (type_out == AstraDB::Metrics::MetricType_Gauge &&
               metric->gauge()) {
      auto gauge = metric->gauge();
      name_out = gauge->name() ? gauge->name()->str() : "";
      value_out = gauge->value();
      return true;
    }

    return false;
  }
};

}  // namespace core
}  // namespace astra
