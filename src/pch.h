// ==============================================================================
// Precompiled Header (PCH) for AstraDB
// ==============================================================================
// This header is precompiled to accelerate compilation times.
// It contains frequently used standard and third-party library headers.
// ==============================================================================

#pragma once

// C++ Standard Library - Most Common Headers (>100 times used)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <type_traits>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <iterator>
#include <cstdio>
#include <array>
#include <tuple>
#include <ostream>

// Abseil Library - Core Components
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>
#include <absl/strings/ascii.h>
#include <absl/strings/numbers.h>
#include <absl/strings/match.h>
#include <absl/strings/str_split.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/inlined_vector.h>
#include <absl/synchronization/mutex.h>
#include <absl/functional/any_invocable.h>
#include <absl/time/time.h>
#include <absl/types/span.h>
#include <absl/hash/hash.h>

// ASIO (Header-only)
#include <asio.hpp>

// SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

// fmt
#include <fmt/format.h>
#include <fmt/ostream.h>

// JSON
#include <nlohmann/json.hpp>

// FlatBuffers
#include <flatbuffers/flatbuffers.h>

// ==============================================================================
// Project Headers - Core Infrastructure
// ==============================================================================
#include "astra/base/logging.hpp"

// ==============================================================================
// Platform-specific includes
// ==============================================================================
#if defined(__linux__) && defined(ASIO_HAS_IO_URING)
// io_uring is Linux-only
#include <liburing.h>
#endif

// ==============================================================================
// Common inline functions
// ==============================================================================

namespace astra {

// Common string utilities
inline std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

inline std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    return s;
}

inline bool Contains(const std::string& str, const std::string& substr) {
    return str.find(substr) != std::string::npos;
}

// Common numeric utilities
template<typename T>
inline constexpr bool IsPowerOfTwo(T v) {
    return v && ((v & (v - 1)) == 0);
}

template<typename T>
inline constexpr T RoundUpToPowerOfTwo(T v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    if constexpr (sizeof(T) > 4) {
        v |= v >> 32;
    }
    v++;
    return v;
}

}  // namespace astra