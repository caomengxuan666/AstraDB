// ==============================================================================
// Vector Search Runtime State
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/synchronization/mutex.h>
#include <absl/synchronization/notification.h>
#include <absl/time/time.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace astra::commands {

enum class VectorSearchMode {
  kLocal = 0,
  kGlobal = 1,
};

enum class VectorSearchJobStatus {
  kPending = 0,
  kDone = 1,
  kCanceled = 2,
  kError = 3,
};

struct VectorSearchJob {
  explicit VectorSearchJob(uint64_t in_job_seq)
      : job_seq(in_job_seq), status(VectorSearchJobStatus::kPending) {}

  uint64_t job_seq = 0;
  bool with_score = false;
  std::string index_name;
  std::vector<float> query_vec;
  size_t k = 0;
  VectorSearchMode mode = VectorSearchMode::kGlobal;

  absl::Time created_at = absl::Now();
  std::atomic<VectorSearchJobStatus> status;
  std::vector<std::pair<uint64_t, float>> results;
  std::string error;

  mutable absl::Mutex mutex;
  absl::Notification completed;
};

class VectorSearchJobTable {
 public:
  std::shared_ptr<VectorSearchJob> CreateJob(bool with_score,
                                             const std::string& index_name,
                                             std::vector<float> query_vec,
                                             size_t k, VectorSearchMode mode) {
    const uint64_t seq = next_seq_.fetch_add(1, std::memory_order_relaxed);
    auto job = std::make_shared<VectorSearchJob>(seq);
    job->with_score = with_score;
    job->index_name = index_name;
    job->query_vec = std::move(query_vec);
    job->k = k;
    job->mode = mode;

    absl::MutexLock lock(&mutex_);
    jobs_[seq] = job;
    return job;
  }

  std::shared_ptr<VectorSearchJob> GetJob(uint64_t seq) const {
    absl::MutexLock lock(&mutex_);
    auto it = jobs_.find(seq);
    if (it == jobs_.end()) {
      return nullptr;
    }
    return it->second;
  }

 private:
  mutable absl::Mutex mutex_;
  absl::flat_hash_map<uint64_t, std::shared_ptr<VectorSearchJob>> jobs_
      ABSL_GUARDED_BY(mutex_);
  std::atomic<uint64_t> next_seq_{1};
};

}  // namespace astra::commands
