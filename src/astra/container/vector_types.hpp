// ==============================================================================
// Vector Types — Core type definitions for vector search
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace astra::container {

enum class DistanceMetric : uint8_t { kCosine = 0, kL2 = 1, kInnerProduct = 2 };

inline std::string DistanceMetricToString(DistanceMetric m) {
  switch (m) {
    case DistanceMetric::kCosine:
      return "cosine";
    case DistanceMetric::kL2:
      return "l2";
    case DistanceMetric::kInnerProduct:
      return "ip";
  }
  return "unknown";
}

inline DistanceMetric DistanceMetricFromString(const std::string& s) {
  if (s == "cosine") return DistanceMetric::kCosine;
  if (s == "l2") return DistanceMetric::kL2;
  if (s == "ip") return DistanceMetric::kInnerProduct;
  return DistanceMetric::kL2;
}

struct IndexConfig {
  std::string name;
  uint32_t dimension = 0;
  DistanceMetric distance_metric = DistanceMetric::kCosine;
  uint32_t M = 16;
  uint32_t ef_construction = 200;
  uint32_t ef_search = 100;
};

using MetaValue = std::variant<int64_t, double, std::string, bool>;

struct VectorEntry {
  std::vector<float> vector;
  std::unordered_map<std::string, MetaValue> metadata;
  std::string index_name;

  VectorEntry() = default;
  VectorEntry(std::vector<float> vec, std::string idx)
      : vector(std::move(vec)), index_name(std::move(idx)) {}
};

}  // namespace astra::container
