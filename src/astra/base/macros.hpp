// ==============================================================================
// Compiler Macros - Cross-Platform
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <cstdint>

// ==============================================================================
// Platform Detection
// ==============================================================================
#if defined(_WIN32) || defined(_WIN64)
#define ASTRADB_PLATFORM_WINDOWS
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_MAC
#define ASTRADB_PLATFORM_MACOS
#elif TARGET_OS_IPHONE
#define ASTRADB_PLATFORM_IOS
#endif
#elif defined(__linux__)
#define ASTRADB_PLATFORM_LINUX
#elif defined(__ANDROID__)
#define ASTRADB_PLATFORM_ANDROID
#else
#define ASTRADB_PLATFORM_UNKNOWN
#endif

// ==============================================================================
// Compiler Detection
// ==============================================================================
#if defined(__clang__)
#define ASTRADB_COMPILER_CLANG
#define ASTRABI_COMPILER_CLANG_VERSION (__clang_major__ * 100 + __clang_minor__)
#elif defined(__GNUC__)
#define ASTRADB_COMPILER_GCC
#define ASTRABI_COMPILER_GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)
#elif defined(_MSC_VER)
#define ASTRADB_COMPILER_MSVC
#define ASTRABI_COMPILER_MSVC
#define ASTRABI_COMPILER_MSVC_VERSION (_MSC_VER)
#else
#define ASTRADB_COMPILER_UNKNOWN
#endif

// ==============================================================================
// Architecture Detection
// ==============================================================================
#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#define ASTRADB_ARCH_X64
#elif defined(__aarch64__) || defined(_M_ARM64)
#define ASTRADB_ARCH_ARM64
#elif defined(__i386) || defined(_M_IX86)
#define ASTRADB_ARCH_X86
#elif defined(__arm__) || defined(_M_ARM)
#define ASTRADB_ARCH_ARM32
#else
#define ASTRADB_ARCH_UNKNOWN
#endif

// ==============================================================================
// Branch Prediction Hints
// ==============================================================================
#if defined(ASTRABI_COMPILER_MSVC)
#define ASTRABI_LIKELY(x) (x)
#define ASTRABI_UNLIKELY(x) (x)
#else
#define ASTRABI_LIKELY(x) __builtin_expect(!!(x), 1)
#define ASTRABI_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

// ==============================================================================
// Function Attributes
// ==============================================================================
#if defined(ASTRABI_COMPILER_MSVC)
#define ASTRABI_FORCE_INLINE __forceinline
#define ASTRABI_NEVER_INLINE __declspec(noinline)
#define ASTRABI_INLINE inline
#else
#define ASTRABI_FORCE_INLINE __attribute__((always_inline)) inline
#define ASTRABI_NEVER_INLINE __attribute__((noinline))
#define ASTRABI_INLINE inline
#endif

// ==============================================================================
// Structure Packing
// ==============================================================================
#if defined(ASTRABI_COMPILER_MSVC)
#define ASTRABI_PACKED_PUSH __pragma(pack(push, 1))
#define ASTRABI_PACKED_POP __pragma(pack(pop))
#define ASTRABI_PACKED
#else
#define ASTRABI_PACKED_PUSH
#define ASTRABI_PACKED_POP
#define ASTRABI_PACKED __attribute__((packed))
#endif

// ==============================================================================
// Deprecated Attribute
// ==============================================================================
#if defined(ASTRABI_COMPILER_MSVC)
#define ASTRABI_DEPRECATED __declspec(deprecated)
#define ASTRABI_DEPRECATED_MSG(msg) __declspec(deprecated(msg))
#else
#define ASTRABI_DEPRECATED __attribute__((deprecated))
#define ASTRABI_DEPRECATED_MSG(msg) __attribute__((deprecated(msg)))
#endif

// ==============================================================================
// Warn Unused Result
// ==============================================================================
#if defined(ASTRABI_COMPILER_MSVC)
#define ASTRABI_WARN_UNUSED_RESULT _Check_return_
#else
#define ASTRABI_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#endif

// ==============================================================================
// Noreturn Attribute
// ==============================================================================
#if defined(ASTRABI_COMPILER_MSVC)
#define ASTRABI_NORETURN __declspec(noreturn)
#else
#define ASTRABI_NORETURN __attribute__((noreturn))
#endif

// ==============================================================================
// Unused Attribute
// ==============================================================================
#if defined(ASTRABI_COMPILER_MSVC)
#define ASTRABI_UNUSED
#else
#define ASTRABI_UNUSED __attribute__((unused))
#endif

// ==============================================================================
// Visibility Attributes
// ==============================================================================
#if defined(ASTRABI_COMPILER_MSVC)
#define ASTRABI_EXPORT __declspec(dllexport)
#define ASTRABI_IMPORT __declspec(dllimport)
#else
#define ASTRABI_EXPORT __attribute__((visibility("default")))
#define ASTRABI_IMPORT
#endif

#if defined(ASTRADB_PLATFORM_WINDOWS)
#ifdef ASTRABI_BUILD_SHARED
#define ASTRABI_PUBLIC ASTRABI_EXPORT
#else
#define ASTRABI_PUBLIC ASTRABI_IMPORT
#endif
#else
#define ASTRABI_PUBLIC __attribute__((visibility("default")))
#endif

// ==============================================================================
// Stringification
// ==============================================================================
#define ASTRABI_STRINGIFY(x) #x
#define ASTRABI_TOSTRING(x) ASTRABI_STRINGIFY(x)

// ==============================================================================
// Concatenation
// ==============================================================================
#define ASTRABI_CONCAT_IMPL(x, y) x##y
#define ASTRABI_CONCAT(x, y) ASTRABI_CONCAT_IMPL(x, y)

// ==============================================================================
// Unique Variable Name Generation
// ==============================================================================
#if defined(ASTRABI_COMPILER_MSVC)
#define ASTRABI_UNIQUE_NAME(prefix) ASTRABI_CONCAT(prefix, __COUNTER__)
#else
#define ASTRABI_UNIQUE_NAME(prefix) ASTRABI_CONCAT(prefix, __COUNTER__)
#endif

// ==============================================================================
// Inline Namespace for ABI Versioning
// ==============================================================================
#define ASTRABI_BEGIN_NAMESPACE(version) inline namespace version {
#define ASTRABI_END_NAMESPACE }

// ==============================================================================
// Disable Copy and Move
// ==============================================================================
#define ASTRABI_DISABLE_COPY(ClassName) \
  ClassName(const ClassName&) = delete; \
  ClassName& operator=(const ClassName&) = delete;

#define ASTRABI_DISABLE_MOVE(ClassName) \
  ClassName(ClassName&&) = delete;      \
  ClassName& operator=(ClassName&&) = delete;

#define ASTRABI_DISABLE_COPY_MOVE(ClassName) \
  ASTRABI_DISABLE_COPY(ClassName)            \
  ASTRABI_DISABLE_MOVE(ClassName)

// ==============================================================================
// Assert Macro
// ==============================================================================
#if defined(NDEBUG)
#define ASTRABI_ASSERT(x) ((void)0)
#else
#if defined(ASTRABI_COMPILER_MSVC)
#define ASTRABI_ASSERT(x) __assume(x)
#else
#define ASTRABI_ASSERT(x) ((x) ? (void)0 : __builtin_unreachable())
#endif
#endif

// ==============================================================================
// Unreachable Macro
// ==============================================================================
#if defined(ASTRABI_COMPILER_MSVC)
#define ASTRABI_UNREACHABLE() __assume(0)
#else
#define ASTRABI_UNREACHABLE() __builtin_unreachable()
#endif

// ==============================================================================
// Thread Local Storage
// ==============================================================================
#if defined(ASTRABI_COMPILER_MSVC)
#define ASTRABI_THREAD_LOCAL __declspec(thread)
#else
#define ASTRABI_THREAD_LOCAL thread_local
#endif

// ==============================================================================
// Cache Line Alignment
// ==============================================================================
#if defined(ASTRABI_COMPILER_MSVC)
#define ASTRABI_CACHE_LINE_ALIGNED __declspec(align(64))
#else
#define ASTRABI_CACHE_LINE_ALIGNED __attribute__((aligned(64)))
#endif

// ==============================================================================
// Likely/Unlikely Macros (Legacy)
// ==============================================================================
#define ASTRADB_LIKELY(x) ASTRABI_LIKELY(x)
#define ASTRADB_UNLIKELY(x) ASTRABI_UNLIKELY(x)
#define ASTRADB_FORCE_INLINE ASTRABI_FORCE_INLINE
#define ASTRADB_NEVER_INLINE ASTRABI_NEVER_INLINE
#define ASTRADB_PACKED ASTRABI_PACKED
#define ASTRADB_DEPRECATED ASTRABI_DEPRECATED

// ==============================================================================
// Legacy Stringification
// ==============================================================================
#define ASTRADB_STRINGIFY(x) ASTRABI_STRINGIFY(x)
#define ASTRADB_TOSTRING(x) ASTRABI_TOSTRING(x)

// ==============================================================================
// Legacy Concatenation
// ==============================================================================
#define ASTRADB_CONCAT_IMPL(x, y) ASTRABI_CONCAT_IMPL(x, y)
#define ASTRADB_CONCAT(x, y) ASTRABI_CONCAT(x, y)

// ==============================================================================
// Legacy Unique Variable Name Generation
// ==============================================================================
#define ASTRADB_UNIQUE_NAME(prefix) ASTRABI_UNIQUE_NAME(prefix)
