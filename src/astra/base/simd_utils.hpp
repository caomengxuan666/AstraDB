// ==============================================================================
// SIMD Utilities - Cross-Platform Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string_view>

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
  #define ASTRABI_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
  #define ASTRABI_PLATFORM_MACOS 1
#elif defined(__linux__)
  #define ASTRABI_PLATFORM_LINUX 1
#endif

// Compiler detection
#if defined(_MSC_VER)
  #define ASTRABI_COMPILER_MSVC 1
#elif defined(__GNUC__)
  #define ASTRABI_COMPILER_GCC 1
#elif defined(__clang__)
  #define ASTRABI_COMPILER_CLANG 1
#endif

// Architecture detection
#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
  #define ASTRABI_ARCH_X86_64 1
  #include <emmintrin.h>   // SSE2
  #include <tmmintrin.h>   // SSSE3
  #include <smmintrin.h>   // SSE4.1
  #include <immintrin.h>   // AVX/AVX2
  #define ASTRABI_HAS_SSE2 1
  #define ASTRABI_HAS_SSE4_2 1
  #if defined(__AVX2__) || (defined(ASTRABI_COMPILER_MSVC) && defined(__AVX2__))
    #define ASTRABI_HAS_AVX2 1
  #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
  #define ASTRABI_ARCH_ARM64 1
  #include <arm_neon.h>
  #define ASTRABI_HAS_NEON 1
#elif defined(__arm__) || defined(_M_ARM)
  #define ASTRABI_ARCH_ARM32 1
  #if defined(__ARM_NEON)
    #include <arm_neon.h>
    #define ASTRABI_HAS_NEON 1
  #endif
#else
  #define ASTRABI_ARCH_UNKNOWN 1
#endif

// Force inline for cross-platform compatibility
#if defined(ASTRABI_COMPILER_MSVC)
  #define ASTRABI_FORCEINLINE __forceinline
#else
  #define ASTRABI_FORCEINLINE __attribute__((always_inline)) inline
#endif

// Suppress warnings on MSVC
#if defined(ASTRABI_COMPILER_MSVC)
  #pragma warning(push)
  #pragma warning(disable : 4244)  // Conversion from size_t to int64_t
  #pragma warning(disable : 4267)  // Conversion from size_t to int
#endif

namespace astra::base::simd {

// ==============================================================================
// SIMD-Aware CRLF Search
// ==============================================================================

// AVX2 implementation
#if defined(ASTRABI_HAS_AVX2)
ASTRABI_FORCEINLINE const char* FindCRLF_AVX2(const char* data, size_t size) {
  const __m256i crlf1 = _mm256_set1_epi16('\r' << 8 | '\n');
  const size_t vec_size = 32;
  const size_t limit = size - 1;
  
  for (size_t i = 0; i + vec_size <= limit; i += vec_size) {
    __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
    __m256i cmp = _mm256_cmpeq_epi16(chunk, crlf1);
    unsigned mask = _mm256_movemask_epi8(cmp);
    
   if (mask != 0) {
      unsigned long pos = 0;
#if defined(ASTRABI_COMPILER_MSVC)
      _BitScanForward(&pos, mask);
#else
      pos = __builtin_ctz(mask);
#endif
     if ((mask & (3 << pos)) != 0) {
        return data + i + static_cast<size_t>(pos);
      }
    }
  }
 return nullptr;
}
#endif

// SSE4.2 implementation
#if defined(ASTRABI_HAS_SSE4_2)
ASTRABI_FORCEINLINE const char* FindCRLF_SSE42(const char* data, size_t size) {
  const __m128i cr = _mm_set1_epi8('\r');
  const size_t vec_size = 16;
  const size_t limit = size -1;
  
  for (size_t i = 0; i + vec_size <= limit; i += vec_size) {
    __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
    __m128i cmp = _mm_cmpeq_epi8(chunk, cr);
    unsigned mask = _mm_movemask_epi8(cmp);
    
   if (mask != 0) {
      unsigned long pos = 0;
#if defined(ASTRABI_COMPILER_MSVC)
      _BitScanForward(&pos, mask);
#else
      pos = __builtin_ctz(mask);
#endif
     if (pos < vec_size -1 && data[i + pos + 1] == '\n') {
        return data + i + static_cast<size_t>(pos);
      }
    }
  }
 return nullptr;
}
#endif

// NEON implementation
#if defined(ASTRABI_HAS_NEON)
ASTRABI_FORCEINLINE const char* FindCRLF_NEON(const char* data, size_t size) {
  const uint8x16_t cr = vdupq_n_u8('\r');
  const uint8x16_t lf = vdupq_n_u8('\n');
  const size_t vec_size = 16;
  const size_t limit = size - 1;
  
  for (size_t i = 0; i + vec_size <= limit; i += vec_size) {
    uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(data + i));
    uint8x16_t cmp_cr = vceqq_u8(chunk, cr);
    uint8x16_t shifted_chunk = vextq_u8(chunk, vdupq_n_u8(0), 1);
    uint8x16_t cmp_lf = vceqq_u8(shifted_chunk, lf);
    uint8x16_t result = vandq_u8(cmp_cr, cmp_lf);
    
   uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(result), 0);
  if (mask != 0) {
      unsigned long pos = 0;
#if defined(ASTRABI_COMPILER_MSVC)
      _BitScanForward64(&pos, mask);
#else
      pos = __builtin_ctzll(mask);
#endif
     return data + i + static_cast<size_t>(pos);
    }
   mask = vget_lane_u64(vreinterpret_u64_u8(result), 1);
  if(mask != 0) {
      unsigned long pos = 0;
#if defined(ASTRABI_COMPILER_MSVC)
      _BitScanForward64(&pos, mask);
#else
      pos = __builtin_ctzll(mask);
#endif
     return data + i + static_cast<size_t>(pos) + 8;
    }
  }
 return nullptr;
}
#endif

// Main CRLF search function
inline const char* FindCRLF(const char* data, size_t size) {
  if (size < 2) {
    return nullptr;
  }

#if defined(ASTRABI_HAS_AVX2)
  if (size >= 32) {
    const char* result = FindCRLF_AVX2(data, size);
    if (result != nullptr) {
      return result;
    }
    // Fall through to check remaining bytes
    size_t processed = (size / 32) * 32;
    if (processed >= size - 1) {
      return nullptr;
    }
    data += processed;
    size -= processed;
  }
#endif

#if defined(ASTRABI_HAS_SSE4_2)
  if (size >= 16) {
    const char* result = FindCRLF_SSE42(data, size);
    if (result != nullptr) {
      return result;
    }
    size_t processed = (size / 16) * 16;
    if (processed >= size - 1) {
      return nullptr;
    }
    data += processed;
    size -= processed;
  }
#endif

#if defined(ASTRABI_HAS_NEON)
  if (size >= 16) {
    const char* result = FindCRLF_NEON(data, size);
    if (result != nullptr) {
      return result;
    }
    size_t processed = (size / 16) * 16;
    if (processed >= size - 1) {
      return nullptr;
    }
    data += processed;
    size -= processed;
  }
#endif

  // Fallback - process remaining bytes
  for (size_t i = 0; i < size - 1; ++i) {
    if (data[i] == '\r' && data[i + 1] == '\n') {
      return data + i;
    }
  }
  
  return nullptr;
}

// ==============================================================================
// SIMD String Comparison
// ==============================================================================

#if defined(ASTRABI_HAS_SSE2)
ASTRABI_FORCEINLINE bool CaseInsensitiveEquals_SSE2(const char* a, const char* b, size_t len) {
  const size_t vec_size = 16;
  
  for (size_t i = 0; i + vec_size <= len; i += vec_size) {
    __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i));
    __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + i));
    
    __m128i mask = _mm_cmpgt_epi8(va, _mm_set1_epi8('Z'));
    __m128i va_lower = _mm_or_si128(va, _mm_and_si128(mask, _mm_set1_epi8(0x20)));
    
    mask = _mm_cmpgt_epi8(vb, _mm_set1_epi8('Z'));
    __m128i vb_lower = _mm_or_si128(vb, _mm_and_si128(mask, _mm_set1_epi8(0x20)));
    
    __m128i cmp = _mm_cmpeq_epi8(va_lower, vb_lower);
    if (_mm_movemask_epi8(cmp) != 0xFFFF) {
      return false;
    }
  }
  return true;
}
#endif

inline bool CaseInsensitiveEquals(const char* a, const char* b, size_t len) {
#if defined(ASTRABI_HAS_SSE2)
  if (len >= 16) {
    if (!CaseInsensitiveEquals_SSE2(a, b, len)) {
      return false;
    }
    size_t processed = (len / 16) * 16;
    a += processed;
    b += processed;
    len -= processed;
  }
#endif

  // Fallback for remaining bytes
  for (size_t i = 0; i < len; ++i) {
    unsigned char ca = static_cast<unsigned char>(a[i]);
    unsigned char cb = static_cast<unsigned char>(b[i]);
    
    if (ca >= 'a' && ca <= 'z') ca -= 32;
    if (cb >= 'a' && cb <= 'z') cb -= 32;
    
    if (ca != cb) {
      return false;
    }
  }
  
  return true;
}

// ==============================================================================
// SIMD String Hashing
// ==============================================================================

inline uint64_t HashString(const char* data, size_t len) {
  uint64_t hash = 14695981039346656037ULL;
  
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint64_t>(data[i]);
    hash *= 1099511628211ULL;
  }
  
  return hash;
}

// ==============================================================================
// SIMD Memory Operations
// ==============================================================================

#if defined(ASTRABI_HAS_AVX2)
ASTRABI_FORCEINLINE int MemCompare_AVX2(const void* a, const void* b, size_t len) {
  const size_t vec_size = 32;
  
  for (size_t i = 0; i + vec_size <= len; i += vec_size) {
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(static_cast<const char*>(a) + i));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(static_cast<const char*>(b) + i));
    
    __m256i cmp = _mm256_cmpeq_epi8(va, vb);
    unsigned mask = _mm256_movemask_epi8(cmp);
    
   if (mask != 0xFFFFFFFF) {
      unsigned long pos = 0;
#if defined(ASTRABI_COMPILER_MSVC)
      _BitScanForward(&pos, ~mask);
#else
      pos = __builtin_ctz(~mask);
#endif
      return static_cast<int>(static_cast<const unsigned char*>(a)[i + pos]) -
             static_cast<int>(static_cast<const unsigned char*>(b)[i + pos]);
    }
  }
 return 0;
}
#endif

#if defined(ASTRABI_HAS_SSE2)
ASTRABI_FORCEINLINE int MemCompare_SSE2(const void* a, const void* b, size_t len) {
  const size_t vec_size = 16;
  
  for (size_t i = 0; i + vec_size <= len; i += vec_size) {
    __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(static_cast<const char*>(a) + i));
    __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(static_cast<const char*>(b) + i));
    
    __m128i cmp = _mm_cmpeq_epi8(va, vb);
    unsigned mask = _mm_movemask_epi8(cmp);
    
    if (mask != 0xFFFF) {
      unsigned long pos = 0;
#if defined(ASTRABI_COMPILER_MSVC)
      _BitScanForward(&pos, ~mask);
#else
      pos = __builtin_ctz(~mask);
#endif
      return static_cast<int>(static_cast<const unsigned char*>(a)[i + pos]) -
             static_cast<int>(static_cast<const unsigned char*>(b)[i + pos]);
    }
  }
  return 0;
}
#endif

inline int MemCompare(const void* a, const void* b, size_t len) {
#if defined(ASTRABI_HAS_AVX2)
  if (len >= 32) {
    int result = MemCompare_AVX2(a, b, len);
    if (result != 0) {
      return result;
    }
    size_t processed = (len / 32) * 32;
    a = static_cast<const char*>(a) + processed;
    b = static_cast<const char*>(b) + processed;
    len -= processed;
  }
#endif

#if defined(ASTRABI_HAS_SSE2)
  if (len >= 16) {
    int result = MemCompare_SSE2(a, b, len);
    if (result != 0) {
      return result;
    }
    size_t processed = (len / 16) * 16;
    a = static_cast<const char*>(a) + processed;
    b = static_cast<const char*>(b) + processed;
    len -= processed;
  }
#endif

  return std::memcmp(a, b, len);
}

// ==============================================================================
// SIMD Zero Check
// ==============================================================================

#if defined(ASTRABI_HAS_AVX2)
ASTRABI_FORCEINLINE bool HasZero_AVX2(const char* data, size_t len) {
  const __m256i zero = _mm256_setzero_si256();
  const size_t vec_size = 32;
  
  for (size_t i = 0; i + vec_size <= len; i += vec_size) {
    __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
    __m256i cmp = _mm256_cmpeq_epi8(chunk, zero);
    unsigned mask = _mm256_movemask_epi8(cmp);
    if (mask != 0) {
      return true;
    }
  }
  return false;
}
#endif

#if defined(ASTRABI_HAS_SSE2)
ASTRABI_FORCEINLINE bool HasZero_SSE2(const char* data, size_t len) {
  const __m128i zero = _mm_setzero_si128();
  const size_t vec_size = 16;
  
  for (size_t i = 0; i + vec_size <= len; i += vec_size) {
    __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
    __m128i cmp = _mm_cmpeq_epi8(chunk, zero);
    unsigned mask = _mm_movemask_epi8(cmp);
    if (mask != 0) {
      return true;
    }
  }
  return false;
}
#endif

inline bool HasZero(const char* data, size_t len) {
#if defined(ASTRABI_HAS_AVX2)
  if (len >= 32 && HasZero_AVX2(data, len)) {
    return true;
  }
  size_t processed = (len / 32) * 32;
  data += processed;
  len -= processed;
#endif

#if defined(ASTRABI_HAS_SSE2)
  if (len >= 16 && HasZero_SSE2(data, len)) {
    return true;
  }
  size_t processed = (len / 16) * 16;
  data += processed;
  len -= processed;
#endif

  for (size_t i = 0; i < len; ++i) {
    if (data[i] == '\0') {
      return true;
    }
  }
  
  return false;
}

}  // namespace astra::base::simd

// Restore warnings on MSVC
#if defined(ASTRABI_COMPILER_MSVC)
  #pragma warning(pop)
#endif
