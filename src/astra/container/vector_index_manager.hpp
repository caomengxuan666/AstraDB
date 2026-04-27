// ==============================================================================
// VectorIndexManager — HNSW index wrapper for per-worker vector search
// ==============================================================================
// License: Apache 2.0
// Architecture: NO SHARING — each worker owns its own set of HNSW indices
// ==============================================================================

#pragma once

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "astra/base/logging.hpp"
#include "hnswlib/hnswlib.h"
#include "vector_types.hpp"

namespace astra::container {

struct IndexStats {
  uint64_t vector_count = 0;
  uint64_t memory_bytes = 0;
  uint32_t dimension = 0;
  DistanceMetric distance_metric = DistanceMetric::kCosine;
  uint32_t M = 16;
  uint32_t ef_construction = 200;
  uint32_t current_ef = 100;
};

class VectorIndexManager {
 public:
  VectorIndexManager() = default;
  ~VectorIndexManager() = default;

  // Disable copy
  VectorIndexManager(const VectorIndexManager&) = delete;
  VectorIndexManager& operator=(const VectorIndexManager&) = delete;

  bool CreateIndex(const IndexConfig& config) {
    if (indexes_.contains(config.name)) {
      return true;
    }

    auto space = CreateDistanceSpace(config.distance_metric, config.dimension);
    if (!space) {
      return false;
    }

    try {
      auto index = std::make_unique<hnswlib::HierarchicalNSW<float>>(
          space.get(), config.M > 0 ? 1000000 : 1000000, config.M,
          config.ef_construction, 100, true);

      index->setEf(config.ef_search);

      IndexInfo info{std::move(space), std::move(index), config};
      info.stats.dimension = config.dimension;
      info.stats.distance_metric = config.distance_metric;
      info.stats.M = config.M;
      info.stats.ef_construction = config.ef_construction;
      info.stats.current_ef = config.ef_search;

      indexes_[config.name] = std::move(info);
      return true;
    } catch (const std::exception& e) {
      return false;
    }
  }

  bool DropIndex(const std::string& name) {
    auto it = indexes_.find(name);
    if (it == indexes_.end()) return false;
    indexes_.erase(it);
    return true;
  }

  bool HasIndex(const std::string& name) const {
    return indexes_.contains(name);
  }

  IndexStats GetStats(const std::string& name) const {
    auto it = indexes_.find(name);
    if (it == indexes_.end()) return {};
    return it->second.stats;
  }

  std::vector<std::string> GetIndexNames() const {
    std::vector<std::string> names;
    names.reserve(indexes_.size());
    for (const auto& [name, info] : indexes_) {
      names.push_back(name);
    }
    return names;
  }

  bool AddVector(const std::string& index_name, uint64_t id,
                 const float* vector) {
    auto it = indexes_.find(index_name);
    if (it == indexes_.end()) return false;

    try {
      it->second.index->addPoint(const_cast<float*>(vector),
                                 static_cast<hnswlib::labeltype>(id),
                                 true);
      it->second.stats.vector_count++;
      return true;
    } catch (const std::exception& e) {
      ASTRADB_LOG_ERROR("HNSW addPoint failed (id={}): {}", id, e.what());
      return false;
    }
  }

  bool RemoveVector(const std::string& index_name, uint64_t id) {
    auto it = indexes_.find(index_name);
    if (it == indexes_.end()) return false;

    try {
      it->second.index->markDelete(static_cast<hnswlib::labeltype>(id));
      return true;
    } catch (const std::exception&) {
      return false;
    }
  }

  std::vector<std::pair<uint64_t, float>> SearchKNN(
      const std::string& index_name, const float* query, size_t k,
      size_t ef_override = 0) {
    auto it = indexes_.find(index_name);
    if (it == indexes_.end()) return {};

    auto& info = it->second;
    if (ef_override > 0) {
      info.index->setEf(ef_override);
    }

    try {
      size_t search_k = std::min(k, static_cast<size_t>(info.stats.vector_count));
      if (search_k == 0) return {};

      auto result = info.index->searchKnn(const_cast<float*>(query),
                                           static_cast<size_t>(search_k));

      std::vector<std::pair<uint64_t, float>> results;
      results.reserve(result.size());

      while (!result.empty()) {
        auto& top = result.top();
        results.push_back(
            {static_cast<uint64_t>(top.second), top.first});
        result.pop();
      }

      std::reverse(results.begin(), results.end());
      return results;
    } catch (const std::exception&) {
      return {};
    }
  }

  void CompactIndex(const std::string& name) {
    auto it = indexes_.find(name);
    if (it == indexes_.end()) return;
    // hnswlib doesn't support compaction natively;
    // rebuild would be needed for physical removal
  }

  void SetEf(const std::string& name, uint32_t ef) {
    auto it = indexes_.find(name);
    if (it == indexes_.end()) return;
    it->second.stats.current_ef = ef;
    it->second.index->setEf(ef);
  }

  size_t TotalVectorCount() const {
    size_t total = 0;
    for (const auto& [name, info] : indexes_) {
      total += info.stats.vector_count;
    }
    return total;
  }

  size_t TotalMemoryBytes() const {
    size_t total = 0;
    for (const auto& [name, info] : indexes_) {
      total += info.stats.memory_bytes;
    }
    return total;
  }

 private:
  static std::unique_ptr<hnswlib::SpaceInterface<float>> CreateDistanceSpace(
      DistanceMetric metric, size_t dim) {
    switch (metric) {
      case DistanceMetric::kL2:
        return std::make_unique<hnswlib::L2Space>(dim);
      case DistanceMetric::kInnerProduct:
        return std::make_unique<hnswlib::InnerProductSpace>(dim);
      case DistanceMetric::kCosine:
        return std::make_unique<hnswlib::InnerProductSpace>(dim);
    }
    return nullptr;
  }

  struct IndexInfo {
    std::unique_ptr<hnswlib::SpaceInterface<float>> space;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> index;
    IndexConfig config;
    IndexStats stats;
  };

  absl::flat_hash_map<std::string, IndexInfo> indexes_;
  mutable std::mutex mutex_;
};

}  // namespace astra::container
