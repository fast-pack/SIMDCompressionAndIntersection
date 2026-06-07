/*
 * neon_sse.h: native ARM NEON (AArch64) implementations of the small set of
 * x86 SSE2/SSE3/SSSE3/SSE4.1/SSE4.2 intrinsics used by this library.
 *
 * This is NOT a general SSE-to-NEON shim: it implements only the ~50 distinct
 * `_mm_*` intrinsics that appear in SIMDCompressionAndIntersection, written
 * directly with arm_neon.h intrinsics. The 256-bit AVX2 code paths are guarded
 * behind `__AVX2__` elsewhere and are not provided here (the SSE/scalar
 * fallbacks are used on ARM instead).
 *
 * Target: AArch64 (ARMv8-A) NEON, which is baseline on Apple Silicon and all
 * 64-bit ARM. A few helpers below use AArch64-only reductions/table lookups
 * (vaddvq, vqtbl1q, ...), so 32-bit ARMv7 is intentionally out of scope.
 *
 * This code is released under the Apache License Version 2.0, matching the
 * rest of the project.
 */
#ifndef SIMDCOMP_NEON_SSE_H_
#define SIMDCOMP_NEON_SSE_H_

#if !(defined(__aarch64__) || defined(_M_ARM64))
#error "neon_sse.h targets AArch64 (ARMv8) NEON only"
#endif

#include <arm_neon.h>
#include <stdint.h>
#include <string.h>

/* 128-bit SSE register types, mapped onto NEON vectors. */
typedef int64x2_t __m128i;
typedef float32x4_t __m128;

#if defined(__GNUC__) || defined(__clang__)
#define SIMDCOMP_NEON_INLINE static inline __attribute__((always_inline))
#else
#define SIMDCOMP_NEON_INLINE static inline
#endif

/* ------------------------------------------------------------------ */
/* Load / store                                                        */
/* ------------------------------------------------------------------ */

SIMDCOMP_NEON_INLINE __m128i _mm_loadu_si128(const __m128i *p) {
  return vreinterpretq_s64_s32(vld1q_s32((const int32_t *)p));
}
SIMDCOMP_NEON_INLINE __m128i _mm_load_si128(const __m128i *p) {
  return vreinterpretq_s64_s32(vld1q_s32((const int32_t *)p));
}
SIMDCOMP_NEON_INLINE __m128i _mm_lddqu_si128(const __m128i *p) {
  return vreinterpretq_s64_s32(vld1q_s32((const int32_t *)p));
}
SIMDCOMP_NEON_INLINE void _mm_storeu_si128(__m128i *p, __m128i a) {
  vst1q_s32((int32_t *)p, vreinterpretq_s32_s64(a));
}
SIMDCOMP_NEON_INLINE void _mm_store_si128(__m128i *p, __m128i a) {
  vst1q_s32((int32_t *)p, vreinterpretq_s32_s64(a));
}
/* NEON has no architected non-temporal store; a plain store is correct. */
SIMDCOMP_NEON_INLINE void _mm_stream_si128(__m128i *p, __m128i a) {
  vst1q_s32((int32_t *)p, vreinterpretq_s32_s64(a));
}
/* Load 64 bits into the low quadword, zero the high quadword. */
SIMDCOMP_NEON_INLINE __m128i _mm_loadl_epi64(const __m128i *p) {
  int64_t v;
  memcpy(&v, p, sizeof(v));
  return vcombine_s64(vdup_n_s64(v), vdup_n_s64(0));
}
/* Store the low 64 bits. */
SIMDCOMP_NEON_INLINE void _mm_storel_epi64(__m128i *p, __m128i a) {
  vst1_s64((int64_t *)p, vget_low_s64(a));
}

/* ------------------------------------------------------------------ */
/* Set / broadcast                                                     */
/* ------------------------------------------------------------------ */

SIMDCOMP_NEON_INLINE __m128i _mm_setzero_si128(void) {
  return vreinterpretq_s64_s32(vdupq_n_s32(0));
}
SIMDCOMP_NEON_INLINE __m128i _mm_set1_epi32(int n) {
  return vreinterpretq_s64_s32(vdupq_n_s32(n));
}
SIMDCOMP_NEON_INLINE __m128i _mm_set1_epi16(short n) {
  return vreinterpretq_s64_s16(vdupq_n_s16(n));
}
SIMDCOMP_NEON_INLINE __m128i _mm_set1_epi8(char n) {
  return vreinterpretq_s64_s8(vdupq_n_s8((int8_t)n));
}
SIMDCOMP_NEON_INLINE __m128i _mm_setr_epi16(short w0, short w1, short w2,
                                            short w3, short w4, short w5,
                                            short w6, short w7) {
  int16_t d[8] = {w0, w1, w2, w3, w4, w5, w6, w7};
  return vreinterpretq_s64_s16(vld1q_s16(d));
}
SIMDCOMP_NEON_INLINE __m128i
_mm_setr_epi8(char b0, char b1, char b2, char b3, char b4, char b5, char b6,
              char b7, char b8, char b9, char b10, char b11, char b12, char b13,
              char b14, char b15) {
  int8_t d[16] = {(int8_t)b0,  (int8_t)b1,  (int8_t)b2,  (int8_t)b3,
                  (int8_t)b4,  (int8_t)b5,  (int8_t)b6,  (int8_t)b7,
                  (int8_t)b8,  (int8_t)b9,  (int8_t)b10, (int8_t)b11,
                  (int8_t)b12, (int8_t)b13, (int8_t)b14, (int8_t)b15};
  return vreinterpretq_s64_s8(vld1q_s8(d));
}
/* _mm_set_epi8 takes its arguments most-significant byte first. */
SIMDCOMP_NEON_INLINE __m128i
_mm_set_epi8(char b15, char b14, char b13, char b12, char b11, char b10,
             char b9, char b8, char b7, char b6, char b5, char b4, char b3,
             char b2, char b1, char b0) {
  int8_t d[16] = {(int8_t)b0,  (int8_t)b1,  (int8_t)b2,  (int8_t)b3,
                  (int8_t)b4,  (int8_t)b5,  (int8_t)b6,  (int8_t)b7,
                  (int8_t)b8,  (int8_t)b9,  (int8_t)b10, (int8_t)b11,
                  (int8_t)b12, (int8_t)b13, (int8_t)b14, (int8_t)b15};
  return vreinterpretq_s64_s8(vld1q_s8(d));
}

/* ------------------------------------------------------------------ */
/* Bitwise logic                                                       */
/* ------------------------------------------------------------------ */

SIMDCOMP_NEON_INLINE __m128i _mm_and_si128(__m128i a, __m128i b) {
  return vreinterpretq_s64_s32(
      vandq_s32(vreinterpretq_s32_s64(a), vreinterpretq_s32_s64(b)));
}
SIMDCOMP_NEON_INLINE __m128i _mm_or_si128(__m128i a, __m128i b) {
  return vreinterpretq_s64_s32(
      vorrq_s32(vreinterpretq_s32_s64(a), vreinterpretq_s32_s64(b)));
}

/* ------------------------------------------------------------------ */
/* Integer arithmetic                                                  */
/* ------------------------------------------------------------------ */

SIMDCOMP_NEON_INLINE __m128i _mm_add_epi32(__m128i a, __m128i b) {
  return vreinterpretq_s64_s32(
      vaddq_s32(vreinterpretq_s32_s64(a), vreinterpretq_s32_s64(b)));
}
SIMDCOMP_NEON_INLINE __m128i _mm_sub_epi32(__m128i a, __m128i b) {
  return vreinterpretq_s64_s32(
      vsubq_s32(vreinterpretq_s32_s64(a), vreinterpretq_s32_s64(b)));
}
/* Low 16 bits of each 16-bit product. */
SIMDCOMP_NEON_INLINE __m128i _mm_mullo_epi16(__m128i a, __m128i b) {
  return vreinterpretq_s64_s16(
      vmulq_s16(vreinterpretq_s16_s64(a), vreinterpretq_s16_s64(b)));
}
SIMDCOMP_NEON_INLINE __m128i _mm_min_epu32(__m128i a, __m128i b) {
  return vreinterpretq_s64_u32(
      vminq_u32(vreinterpretq_u32_s64(a), vreinterpretq_u32_s64(b)));
}
SIMDCOMP_NEON_INLINE __m128i _mm_max_epu32(__m128i a, __m128i b) {
  return vreinterpretq_s64_u32(
      vmaxq_u32(vreinterpretq_u32_s64(a), vreinterpretq_u32_s64(b)));
}

/* ------------------------------------------------------------------ */
/* Comparisons                                                         */
/* ------------------------------------------------------------------ */

SIMDCOMP_NEON_INLINE __m128i _mm_cmpeq_epi32(__m128i a, __m128i b) {
  return vreinterpretq_s64_u32(
      vceqq_s32(vreinterpretq_s32_s64(a), vreinterpretq_s32_s64(b)));
}
SIMDCOMP_NEON_INLINE __m128i _mm_cmpeq_epi16(__m128i a, __m128i b) {
  return vreinterpretq_s64_u16(
      vceqq_s16(vreinterpretq_s16_s64(a), vreinterpretq_s16_s64(b)));
}
SIMDCOMP_NEON_INLINE __m128i _mm_cmplt_epi32(__m128i a, __m128i b) {
  return vreinterpretq_s64_u32(
      vcltq_s32(vreinterpretq_s32_s64(a), vreinterpretq_s32_s64(b)));
}

/* ------------------------------------------------------------------ */
/* Shifts by a scalar count                                            */
/*                                                                     */
/* SSE defines an out-of-range count (>= element width) as producing   */
/* zero; a count of 0 returns the input unchanged. The compile-time    */
/* constant counts used throughout the bit-packing code let the        */
/* optimizer fold the guards and emit a single immediate shift.        */
/* ------------------------------------------------------------------ */

SIMDCOMP_NEON_INLINE __m128i _mm_slli_epi32(__m128i a, int imm) {
  if (imm <= 0)
    return a;
  if (imm > 31)
    return _mm_setzero_si128();
  return vreinterpretq_s64_s32(
      vshlq_s32(vreinterpretq_s32_s64(a), vdupq_n_s32(imm)));
}
SIMDCOMP_NEON_INLINE __m128i _mm_srli_epi32(__m128i a, int imm) {
  if (imm <= 0)
    return a;
  if (imm > 31)
    return _mm_setzero_si128();
  return vreinterpretq_s64_u32(
      vshlq_u32(vreinterpretq_u32_s64(a), vdupq_n_s32(-imm)));
}
SIMDCOMP_NEON_INLINE __m128i _mm_slli_epi64(__m128i a, int imm) {
  if (imm <= 0)
    return a;
  if (imm > 63)
    return _mm_setzero_si128();
  return vshlq_s64(a, vdupq_n_s64(imm));
}
SIMDCOMP_NEON_INLINE __m128i _mm_srli_epi64(__m128i a, int imm) {
  if (imm <= 0)
    return a;
  if (imm > 63)
    return _mm_setzero_si128();
  return vreinterpretq_s64_u64(
      vshlq_u64(vreinterpretq_u64_s64(a), vdupq_n_s64(-imm)));
}
SIMDCOMP_NEON_INLINE __m128i _mm_srli_epi16(__m128i a, int imm) {
  if (imm <= 0)
    return a;
  if (imm > 15)
    return _mm_setzero_si128();
  return vreinterpretq_s64_u16(
      vshlq_u16(vreinterpretq_u16_s64(a), vdupq_n_s16((int16_t)-imm)));
}

/* ------------------------------------------------------------------ */
/* Whole-register byte shifts and alignment                            */
/* ------------------------------------------------------------------ */

/* Shift left by imm bytes, shifting in zeros (toward higher lanes). */
SIMDCOMP_NEON_INLINE __m128i _mm_slli_si128(__m128i a, int imm) {
  uint8_t in[16], out[16];
  int i;
  if (imm <= 0)
    return a;
  if (imm > 15)
    return _mm_setzero_si128();
  vst1q_u8(in, vreinterpretq_u8_s64(a));
  for (i = 0; i < 16; i++)
    out[i] = (i >= imm) ? in[i - imm] : 0;
  return vreinterpretq_s64_u8(vld1q_u8(out));
}
/* Shift right by imm bytes, shifting in zeros (toward lower lanes). */
SIMDCOMP_NEON_INLINE __m128i _mm_srli_si128(__m128i a, int imm) {
  uint8_t in[16], out[16];
  int i;
  if (imm <= 0)
    return a;
  if (imm > 15)
    return _mm_setzero_si128();
  vst1q_u8(in, vreinterpretq_u8_s64(a));
  for (i = 0; i < 16; i++)
    out[i] = (i + imm < 16) ? in[i + imm] : 0;
  return vreinterpretq_s64_u8(vld1q_u8(out));
}
/* Concatenate a:b (a high, b low) and shift right by imm bytes. */
SIMDCOMP_NEON_INLINE __m128i _mm_alignr_epi8(__m128i a, __m128i b, int imm) {
  uint8_t cat[32], out[16];
  int i;
  if (imm >= 32)
    return _mm_setzero_si128();
  vst1q_u8(cat, vreinterpretq_u8_s64(b));
  vst1q_u8(cat + 16, vreinterpretq_u8_s64(a));
  for (i = 0; i < 16; i++)
    out[i] = (i + imm < 32) ? cat[i + imm] : 0;
  return vreinterpretq_s64_u8(vld1q_u8(out));
}

/* ------------------------------------------------------------------ */
/* Shuffles                                                            */
/* ------------------------------------------------------------------ */

/* Byte shuffle (pshufb): for each output byte, the low 4 bits of the
 * control select a source byte; if the control's high bit is set, the
 * output byte is zero. vqtbl1q_u8 returns 0 for indices >= 16, so masking
 * the control with 0x8F reproduces both behaviours exactly. */
SIMDCOMP_NEON_INLINE __m128i _mm_shuffle_epi8(__m128i a, __m128i b) {
  uint8x16_t tbl = vreinterpretq_u8_s64(a);
  uint8x16_t idx = vreinterpretq_u8_s64(b);
  uint8x16_t masked = vandq_u8(idx, vdupq_n_u8(0x8F));
  return vreinterpretq_s64_u8(vqtbl1q_u8(tbl, masked));
}

/* Build the 8-bit control operand for _mm_shuffle_epi32 / _mm_shuffle_ps. */
#ifndef _MM_SHUFFLE
#define _MM_SHUFFLE(z, y, x, w)                                                \
  (((z) << 6) | ((y) << 4) | ((x) << 2) | (w))
#endif

/* Select four 32-bit lanes; two control bits per output lane. */
SIMDCOMP_NEON_INLINE __m128i _mm_shuffle_epi32_impl(__m128i a, int imm) {
  int32_t s[4], r[4];
  vst1q_s32(s, vreinterpretq_s32_s64(a));
  r[0] = s[imm & 3];
  r[1] = s[(imm >> 2) & 3];
  r[2] = s[(imm >> 4) & 3];
  r[3] = s[(imm >> 6) & 3];
  return vreinterpretq_s64_s32(vld1q_s32(r));
}
#define _mm_shuffle_epi32(a, imm) _mm_shuffle_epi32_impl((a), (imm))

/* ------------------------------------------------------------------ */
/* Conversions / extraction                                            */
/* ------------------------------------------------------------------ */

SIMDCOMP_NEON_INLINE int _mm_cvtsi128_si32(__m128i a) {
  return vgetq_lane_s32(vreinterpretq_s32_s64(a), 0);
}
SIMDCOMP_NEON_INLINE __m128i _mm_cvtsi32_si128(int n) {
  return vreinterpretq_s64_s32(vsetq_lane_s32(n, vdupq_n_s32(0), 0));
}
/* Zero-extend the low eight unsigned bytes to eight 16-bit lanes. */
SIMDCOMP_NEON_INLINE __m128i _mm_cvtepu8_epi16(__m128i a) {
  return vreinterpretq_s64_u16(vmovl_u8(vget_low_u8(vreinterpretq_u8_s64(a))));
}
/* Zero-extend the low four unsigned 16-bit lanes to four 32-bit lanes. */
SIMDCOMP_NEON_INLINE __m128i _mm_cvtepu16_epi32(__m128i a) {
  return vreinterpretq_s64_u32(
      vmovl_u16(vget_low_u16(vreinterpretq_u16_s64(a))));
}
/* Sign-extend the low four signed bytes to four 32-bit lanes. */
SIMDCOMP_NEON_INLINE __m128i _mm_cvtepi8_epi32(__m128i a) {
  int16x8_t s16 = vmovl_s8(vget_low_s8(vreinterpretq_s8_s64(a)));
  return vreinterpretq_s64_s32(vmovl_s16(vget_low_s16(s16)));
}
#define _mm_extract_epi32(a, imm)                                              \
  vgetq_lane_s32(vreinterpretq_s32_s64(a), (imm))

/* ------------------------------------------------------------------ */
/* Float reinterpretation and mask extraction                          */
/* ------------------------------------------------------------------ */

SIMDCOMP_NEON_INLINE __m128 _mm_castsi128_ps(__m128i a) {
  return vreinterpretq_f32_s64(a);
}
/* Gather the most-significant bit of each byte into a 16-bit mask. */
SIMDCOMP_NEON_INLINE int _mm_movemask_epi8(__m128i a) {
  const uint8_t md[16] = {1, 2, 4, 8, 16, 32, 64, 128,
                          1, 2, 4, 8, 16, 32, 64, 128};
  uint8x16_t input = vreinterpretq_u8_s64(a);
  uint8x16_t highbit = vshrq_n_u8(input, 7);
  uint8x16_t weighted = vmulq_u8(highbit, vld1q_u8(md));
  int lo = vaddv_u8(vget_low_u8(weighted));
  int hi = vaddv_u8(vget_high_u8(weighted));
  return lo | (hi << 8);
}
/* Gather the most-significant bit of each 32-bit lane into a 4-bit mask. */
SIMDCOMP_NEON_INLINE int _mm_movemask_ps(__m128 a) {
  const uint32_t md[4] = {1, 2, 4, 8};
  uint32x4_t input = vreinterpretq_u32_f32(a);
  uint32x4_t highbit = vshrq_n_u32(input, 31);
  uint32x4_t weighted = vmulq_u32(highbit, vld1q_u32(md));
  return (int)vaddvq_u32(weighted);
}

/* ------------------------------------------------------------------ */
/* Tests / population count                                            */
/* ------------------------------------------------------------------ */

SIMDCOMP_NEON_INLINE int _mm_testz_si128(__m128i a, __m128i b) {
  int64x2_t t = vandq_s64(a, b);
  return (vgetq_lane_s64(t, 0) | vgetq_lane_s64(t, 1)) == 0;
}
SIMDCOMP_NEON_INLINE int _mm_popcnt_u32(unsigned int v) {
  return __builtin_popcount(v);
}

/* ------------------------------------------------------------------ */
/* SSE4.2 string-compare subset (PCMPESTRM / PCMPISTRM)                */
/*                                                                     */
/* Only the mode actually used by the library is implemented:          */
/*   _SIDD_UWORD_OPS | _SIDD_CMP_EQUAL_ANY | _SIDD_BIT_MASK            */
/* i.e. for each 16-bit element of the second operand, set the         */
/* corresponding result bit when it equals any element of the first.   */
/* The result is a bit mask in the low lane (read via                  */
/* _mm_extract_epi32(res, 0)).                                          */
/* ------------------------------------------------------------------ */

#ifndef _SIDD_UBYTE_OPS
#define _SIDD_UBYTE_OPS 0x00
#define _SIDD_UWORD_OPS 0x01
#define _SIDD_SBYTE_OPS 0x02
#define _SIDD_SWORD_OPS 0x03
#define _SIDD_CMP_EQUAL_ANY 0x00
#define _SIDD_CMP_RANGES 0x04
#define _SIDD_CMP_EQUAL_EACH 0x08
#define _SIDD_CMP_EQUAL_ORDERED 0x0C
#define _SIDD_POSITIVE_POLARITY 0x00
#define _SIDD_NEGATIVE_POLARITY 0x10
#define _SIDD_MASKED_POSITIVE_POLARITY 0x20
#define _SIDD_MASKED_NEGATIVE_POLARITY 0x30
#define _SIDD_LEAST_SIGNIFICANT 0x00
#define _SIDD_MOST_SIGNIFICANT 0x40
#define _SIDD_BIT_MASK 0x00
#define _SIDD_UNIT_MASK 0x40
#endif

SIMDCOMP_NEON_INLINE __m128i _mm_cmpestrm(__m128i a, int la, __m128i b, int lb,
                                          const int imm8) {
  uint16x8_t va = vreinterpretq_u16_s64(a);
  uint16x8_t vb = vreinterpretq_u16_s64(b);
  uint16_t needles[8];
  uint16_t matched[8];
  int na = la < 0 ? -la : la;
  int nb = lb < 0 ? -lb : lb;
  uint16x8_t acc = vdupq_n_u16(0);
  int j, i, r = 0;
  (void)imm8; /* only EQUAL_ANY / UWORD / BIT_MASK is used */
  if (na > 8)
    na = 8;
  if (nb > 8)
    nb = 8;
  vst1q_u16(needles, va);
  for (j = 0; j < na; j++)
    acc = vorrq_u16(acc, vceqq_u16(vb, vdupq_n_u16(needles[j])));
  vst1q_u16(matched, acc);
  for (i = 0; i < nb; i++)
    if (matched[i])
      r |= 1 << i;
  return _mm_cvtsi32_si128(r);
}

SIMDCOMP_NEON_INLINE __m128i _mm_cmpistrm(__m128i a, __m128i b,
                                          const int imm8) {
  /* Implicit lengths: each operand ends at its first zero element. */
  uint16_t A[8], B[8];
  int la = 8, lb = 8, i;
  vst1q_u16(A, vreinterpretq_u16_s64(a));
  vst1q_u16(B, vreinterpretq_u16_s64(b));
  for (i = 0; i < 8; i++)
    if (A[i] == 0) {
      la = i;
      break;
    }
  for (i = 0; i < 8; i++)
    if (B[i] == 0) {
      lb = i;
      break;
    }
  return _mm_cmpestrm(a, la, b, lb, imm8);
}

#endif /* SIMDCOMP_NEON_SSE_H_ */
