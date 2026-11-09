#pragma once

/**
 * @file sse_neon.h
 * @brief The subset of the SSE/SSE2 intrinsics used by the engine, implemented with NEON.
 *
 * Only included on arm64. Each function keeps the x86 result exactly, including the cases where
 * the obvious NEON instruction differs:
 *   - _mm_max_ps returns the second operand when either input is NaN (vmaxq_f32 returns NaN);
 *   - the *_ss operations only change lane 0 and take the other lanes from the first operand;
 *   - _mm_set_ps takes its arguments from the highest lane to the lowest;
 *   - __m128 and __m128i may alias other types, which the engine relies on when it casts int and
 *     float arrays to vector pointers.
 */
#include <arm_neon.h>

typedef float32x4_t __m128 __attribute__((may_alias));
typedef int32x4_t __m128i __attribute__((may_alias));

#define SSE_NEON_INLINE static inline __attribute__((always_inline))

// float: load, store, set

SSE_NEON_INLINE __m128 _mm_load_ps(const float* p) { return vld1q_f32(p); }
SSE_NEON_INLINE __m128 _mm_loadu_ps(const float* p) { return vld1q_f32(p); }
SSE_NEON_INLINE __m128 _mm_load1_ps(const float* p) { return vld1q_dup_f32(p); }
SSE_NEON_INLINE __m128 _mm_load_ss(const float* p) { return vsetq_lane_f32(*p, vdupq_n_f32(0.0f), 0); }
SSE_NEON_INLINE void _mm_store_ps(float* p, __m128 a) { vst1q_f32(p, a); }
SSE_NEON_INLINE void _mm_storeu_ps(float* p, __m128 a) { vst1q_f32(p, a); }
SSE_NEON_INLINE void _mm_store_ss(float* p, __m128 a) { vst1q_lane_f32(p, a, 0); }
SSE_NEON_INLINE __m128 _mm_setzero_ps() { return vdupq_n_f32(0.0f); }
SSE_NEON_INLINE __m128 _mm_set1_ps(float v) { return vdupq_n_f32(v); }
SSE_NEON_INLINE __m128 _mm_set_ss(float v) { return vsetq_lane_f32(v, vdupq_n_f32(0.0f), 0); }

SSE_NEON_INLINE __m128 _mm_set_ps(float e3, float e2, float e1, float e0)
{
    const float v[4] = { e0, e1, e2, e3 };
    return vld1q_f32(v);
}

// float: arithmetic

SSE_NEON_INLINE __m128 _mm_add_ps(__m128 a, __m128 b) { return vaddq_f32(a, b); }
SSE_NEON_INLINE __m128 _mm_mul_ps(__m128 a, __m128 b) { return vmulq_f32(a, b); }
SSE_NEON_INLINE __m128 _mm_div_ps(__m128 a, __m128 b) { return vdivq_f32(a, b); }

SSE_NEON_INLINE __m128 _mm_add_ss(__m128 a, __m128 b)
{
    return vsetq_lane_f32(vgetq_lane_f32(a, 0) + vgetq_lane_f32(b, 0), a, 0);
}

SSE_NEON_INLINE __m128 _mm_sub_ss(__m128 a, __m128 b)
{
    return vsetq_lane_f32(vgetq_lane_f32(a, 0) - vgetq_lane_f32(b, 0), a, 0);
}

SSE_NEON_INLINE __m128 _mm_mul_ss(__m128 a, __m128 b)
{
    return vsetq_lane_f32(vgetq_lane_f32(a, 0) * vgetq_lane_f32(b, 0), a, 0);
}

SSE_NEON_INLINE __m128 _mm_max_ps(__m128 a, __m128 b)
{
    return vbslq_f32(vcgtq_f32(a, b), a, b);
}

// float: bitwise and comparison

SSE_NEON_INLINE __m128 _mm_and_ps(__m128 a, __m128 b)
{
    return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
}

SSE_NEON_INLINE __m128 _mm_cmplt_ps(__m128 a, __m128 b)
{
    return vreinterpretq_f32_u32(vcltq_f32(a, b));
}

SSE_NEON_INLINE int _mm_movemask_ps(__m128 a)
{
    const uint32x4_t sign = vshrq_n_u32(vreinterpretq_u32_f32(a), 31);
    return (int)(vgetq_lane_u32(sign, 0)
        | (vgetq_lane_u32(sign, 1) << 1)
        | (vgetq_lane_u32(sign, 2) << 2)
        | (vgetq_lane_u32(sign, 3) << 3));
}

// integer

SSE_NEON_INLINE __m128i _mm_set1_epi32(int v) { return vdupq_n_s32(v); }
SSE_NEON_INLINE __m128i _mm_add_epi32(__m128i a, __m128i b) { return vaddq_s32(a, b); }
SSE_NEON_INLINE __m128i _mm_and_si128(__m128i a, __m128i b) { return vandq_s32(a, b); }

// A shift count of 32 or more gives 0 on both architectures. vshlq shifts right on a negative count.
SSE_NEON_INLINE __m128i _mm_srli_epi32(__m128i a, int count)
{
    return vreinterpretq_s32_u32(vshlq_u32(vreinterpretq_u32_s32(a), vdupq_n_s32(-count)));
}
