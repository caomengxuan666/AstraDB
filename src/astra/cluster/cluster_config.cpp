// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "astra/cluster/cluster_config.hpp"

#include "absl/time/clock.h"

namespace astra::cluster {

// Define thread-local static members
thread_local std::shared_ptr<ClusterState> ClusterStateAccessor::state_ptr_ = nullptr;
thread_local ClusterState* ClusterStateAccessor::state_ = nullptr;

}  // namespace astra::cluster