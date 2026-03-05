// ==============================================================================
// Precompiled Header (PCH) for AstraDB
// ==============================================================================
// This header is precompiled to accelerate compilation times.
// It contains frequently used standard and third-party library headers.
// ==============================================================================

#pragma once

// C++ Standard Library
#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <bitset>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <compare>
#include <complex>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <filesystem>
#include <forward_list>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <ios>
#include <iosfwd>
#include <iostream>
#include <istream>
#include <iterator>
#include <limits>
#include <list>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <ostream>
#include <queue>
#include <random>
#include <ratio>
#include <regex>
#include <scoped_allocator>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// Abseil Library
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/inlined_vector.h>
#include <absl/synchronization/mutex.h>
#include <absl/synchronization/notification.h>
#include <absl/functional/any_invocable.h>
#include <absl/base/attributes.h>
#include <absl/base/optimization.h>
#include <absl/time/clock.h>
#include <absl/time/time.h>
#include <absl/types/optional.h>
#include <absl/types/span.h>
#include <absl/types/variant.h>
#include <absl/numeric/int128.h>
#include <absl/crc/crc32c.h>
#include <absl/strings/match.h>
#include <absl/strings/ascii.h>

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

// Date
#include <date/date.h>

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