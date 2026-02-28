// ==============================================================================
// Compiler Macros
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <cstdint>

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
  #define ASTRADB_PLATFORM_WINDOWS
#elif defined(__APPLE__)
  #define ASTRADB_PLATFORM_MACOS
#elif defined(__linux__)
  #define ASTRADB_PLATFORM_LINUX
#else
  #define ASTRADB_PLATFORM_UNKNOWN
#endif

// Compiler detection
#if defined(__clang__)
  #define ASTRADB_COMPILER_CLANG
#elif defined(__GNUC__)
  #define ASTRADB_COMPILER_GCC
#elif defined(_MSC_VER)
  #define ASTRADB_COMPILER_MSVC
#else
  #define ASTRADB_COMPILER_UNKNOWN
#endif

// Architecture detection
#if defined(__x86_64__) || defined(_M_X64)
  #define ASTRADB_ARCH_X64
#elif defined(__aarch64__) || defined(_M_ARM64)
  #define ASTRADB_ARCH_ARM64
#elif defined(__i386) || defined(_M_IX86)
  #define ASTRADB_ARCH_X86
#else
  #define ASTRADB_ARCH_UNKNOWN
#endif

// Utility macros
#define ASTRADB_LIKELY(x) __builtin_expect(!!(x), 1)
#define ASTRADB_UNLIKELY(x) __builtin_expect(!!(x), 0)

#define ASTRADB_FORCE_INLINE inline __attribute__((always_inline))
#define ASTRADB_NEVER_INLINE __attribute__((noinline))

#define ASTRADB_PACKED __attribute__((packed))

// Stringification
#define ASTRADB_STRINGIFY(x) #x
#define ASTRADB_TOSTRING(x) ASTRADB_STRINGIFY(x)

// Concatenation
#define ASTRADB_CONCAT_IMPL(x, y) x##y
#define ASTRADB_CONCAT(x, y) ASTRADB_CONCAT_IMPL(x, y)

// Unique variable name generation
#define ASTRADB_UNIQUE_NAME(prefix) ASTRADB_CONCAT(prefix, __COUNTER__)

// Attribute macros
#if defined(ASTRADB_COMPILER_CLANG) || defined(ASTRADB_COMPILER_GCC)
  #define ASTRABI_DEPRECATED __attribute__((deprecated))
  #define ASTRABI_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
  #define ASTRABI_DEPRECATED
  #define ASTRABI_WARN_UNUSED_RESULT
#endif

// Inline namespace for ABI versioning
#define ASTRABI_BEGIN_NAMESPACE(version) inline namespace version {
#define ASTRABI_END_NAMESPACE }

// Disable copy and move
#define ASTRABI_DISABLE_COPY(ClassName) \
  ClassName(const ClassName&) = delete; \
  ClassName& operator=(const ClassName&) = delete;

#define ASTRABI_DISABLE_MOVE(ClassName) \
  ClassName(ClassName&&) = delete; \
  ClassName& operator=(ClassName&&) = delete;

#define ASTRABI_DISABLE_COPY_MOVE(ClassName) \
  ASTRABI_DISABLE_COPY(ClassName) \
  ASTRABI_DISABLE_MOVE(ClassName)