/* platform.h: Cross-platform macros and compatibility shims. */

#pragma once

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* SIMD intrinsics                                                     */
/*                                                                     */
/* On x86/x64 we pull in the native Intel intrinsics. On AArch64 we    */
/* use neon_sse.h, which implements the SSE intrinsics used by this    */
/* library directly with ARM NEON. To keep the existing                */
/* `#ifdef __SSE4_1__` / `__SSSE3__` feature gates active on ARM (so   */
/* the SIMD code paths are selected instead of the scalar fallbacks),  */
/* we advertise the SSE feature levels that neon_sse.h provides.       */
/* __AVX2__ is deliberately left undefined: the 256-bit paths fall     */
/* back to the 128-bit SSE/NEON ones.                                  */
/* ------------------------------------------------------------------ */
#if defined(__aarch64__) || defined(_M_ARM64)
#ifndef __SSE2__
#define __SSE2__ 1
#endif
#ifndef __SSE3__
#define __SSE3__ 1
#endif
#ifndef __SSSE3__
#define __SSSE3__ 1
#endif
#ifndef __SSE4_1__
#define __SSE4_1__ 1
#endif
#ifndef __SSE4_2__
#define __SSE4_2__ 1
#endif
#include "neon_sse.h"
#elif defined(_MSC_VER)
#include <intrin.h>
#else
#include <immintrin.h>
#endif

#if defined(_MSC_VER)
#define ALWAYS_INLINE __forceinline
#define CONST_FUNCTION
#define PURE_FUNCTION
#define __restrict__ __restrict
#define SIMDCOMP_ALIGNED(x) __declspec(align(x))
#else
#if defined(__GNUC__)
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define CONST_FUNCTION __attribute__((const))
#define PURE_FUNCTION __attribute__((pure))
#define SIMDCOMP_ALIGNED(x) __attribute__((aligned(x)))
#endif
#endif

#ifdef _MSC_VER
#include <intrin.h>

int __inline __builtin_clz(uint32_t value) {
  unsigned long leading_zero = 0;
  return _BitScanReverse(&leading_zero, value) == 0 ? 0 : (31 - leading_zero);
}

int __inline __builtin_ctz(uint32_t value) {
  unsigned long trailing_zero = 0;
  return _BitScanForward(&trailing_zero, value) == 0 ? 32 : trailing_zero;
}

// NOTE: avoid ctzl / clzl since long is 32 bits for MSVC

int __inline __builtin_ctzll(uint64_t value) {
#ifdef _M_X64
  unsigned long trailing_zero = 0;
  return _BitScanForward64(&trailing_zero, value) == 0 ? 64 : trailing_zero;
#else
  return ((value & 0xFFFFFFFF) == 0) ? (__builtin_ctz(value >> 32) + 32)
                                     : __builtin_ctz(value & 0xFFFFFFFF);
#endif
}
#endif
