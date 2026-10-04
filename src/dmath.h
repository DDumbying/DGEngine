#ifndef SLIME_DMATH_H
#define SLIME_DMATH_H

/* Math that gives the same bits on every platform. IEEE 754 requires + - * / and sqrt to be correctly rounded,
   and floor, ceil, round, fabs and conversions are exact, so those are used freely. Everything else libm
   offers (exp, log, pow, sin, cos, cbrt) differs between platforms, so slime carries its own, built only
   from the exact operations above. They are accurate to a few units in the last place, which is plenty;
   what matters is that every machine gets the same answer. */

#include <math.h>
#include <stdint.h>
#include <string.h>

#if defined(__FAST_MATH__)
#error "slime needs IEEE floating point to be deterministic; build it without -ffast-math"
#endif
#if (defined(__i386__) && !defined(__SSE2_MATH__)) || (defined(_M_IX86) && (!defined(_M_IX86_FP) || _M_IX86_FP < 2))
#error "32-bit x86 builds of slime need SSE2 math (-msse2 -mfpmath=sse); x87 arithmetic is not deterministic"
#endif
/* Fused multiply-add rounds once where the source rounds twice; CMake passes -ffp-contract=off, and these
   cover builds that compile the sources some other way. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

/* Signed zeros and NaN are handled the same way everywhere, unlike fminf and fmaxf; the first argument is
   returned when the second does not compare smaller or larger, so accumulators keep their value on NaN. */
static inline float sl_min(float a, float b) { return b < a ? b : a; }
static inline float sl_max(float a, float b) { return b > a ? b : a; }

static inline float sl_from_bits(uint32_t bits) { float f; memcpy(&f, &bits, 4); return f; }
static inline uint32_t sl_bits(float f) { uint32_t bits; memcpy(&bits, &f, 4); return bits; }

/* 2^n for integer n, clamped to the normal range. */
static inline float sl_pow2i(int n) {
    if (n < -126) n = -126;
    if (n > 127) n = 127;
    return sl_from_bits((uint32_t)(n + 127) << 23);
}

/* 2^x: x = n + f with f in [-0.5, 0.5], then a degree 7 series for e^(f ln 2). */
static inline float sl_exp2(float x) {
    if (x != x) return x;
    if (x < -126.0f) return 0.0f;
    if (x > 127.0f) x = 127.0f;
    float n = floorf(x + 0.5f), f = (x - n) * 0.693147180559945f;
    float p = 1.0f / 5040.0f;
    p = p * f + 1.0f / 720.0f;
    p = p * f + 1.0f / 120.0f;
    p = p * f + 1.0f / 24.0f;
    p = p * f + 1.0f / 6.0f;
    p = p * f + 0.5f;
    p = p * f + 1.0f;
    p = p * f + 1.0f;
    return p * sl_pow2i((int)n);
}

/* log2(x) for x > 0: x = m 2^e with m in [sqrt(1/2), sqrt(2)), then the atanh series for ln(m). */
static inline float sl_log2(float x) {
    if (!(x > 0.0f)) return x == 0.0f ? -INFINITY : NAN;
    uint32_t bits = sl_bits(x);
    int e = (int)(bits >> 23) - 127;
    if (e == -127) {   /* subnormal: scale into the normal range first */
        bits = sl_bits(x * 8388608.0f);
        e = (int)(bits >> 23) - 127 - 23;
    }
    float m = sl_from_bits((bits & 0x007fffffu) | 0x3f800000u);
    if (m > 1.41421356f) { m *= 0.5f; e++; }
    float s = (m - 1.0f) / (m + 1.0f), s2 = s * s;
    float p = 1.0f / 9.0f;
    p = p * s2 + 1.0f / 7.0f;
    p = p * s2 + 1.0f / 5.0f;
    p = p * s2 + 1.0f / 3.0f;
    p = p * s2 + 1.0f;
    return (float)e + 2.0f * s * p * 1.44269504088896f;
}

/* a^y for a >= 0; 0^y is 0. */
static inline float sl_pow(float a, float y) {
    if (a <= 0.0f) return 0.0f;
    return sl_exp2(y * sl_log2(a));
}

/* a^k for integer k >= 0, by repeated products in a fixed order. */
static inline float sl_powi(float a, int k) {
    float r = 1.0f;
    for (int i = 0; i < k; i++) r *= a;
    return r;
}

/* sin and cos together: x = k pi/2 + r with r in [-pi/4, pi/4] (pi/2 split in two so k pi/2 subtracts exactly
   for |k| < 2^15), then odd and even series for r. */
static inline void sl_sincos(float x, float *s, float *c) {
    float k = floorf(x * 0.636619772367581f + 0.5f);
    float r = (x - k * 1.5703125f) - k * 4.83826794897e-4f;
    float r2 = r * r;
    float sp = -1.0f / 5040.0f;
    sp = sp * r2 + 1.0f / 120.0f;
    sp = sp * r2 - 1.0f / 6.0f;
    sp = sp * r2 * r + r;
    float cp = 1.0f / 40320.0f;
    cp = cp * r2 - 1.0f / 720.0f;
    cp = cp * r2 + 1.0f / 24.0f;
    cp = cp * r2 - 0.5f;
    cp = cp * r2 + 1.0f;
    int q = (int)(k - 4.0f * floorf(k * 0.25f));   /* quadrant 0..3 */
    switch (q) {
    case 0: *s = sp; *c = cp; break;
    case 1: *s = cp; *c = -sp; break;
    case 2: *s = -sp; *c = -cp; break;
    default: *s = -cp; *c = sp; break;
    }
}

/* Cube root for x > 0: a guess from the exponent, then Newton steps. */
static inline float sl_cbrt(float x) {
    if (!(x > 0.0f)) return 0.0f;
    float y = sl_from_bits(sl_bits(x) / 3u + 0x2a51067fu);
    for (int i = 0; i < 4; i++) y = y - (y * y * y - x) / (3.0f * y * y);
    return y;
}

/* Largest integer m >= 1 with m^k <= v, for 1 <= k <= 3; the products stay exact in double. */
static inline double sl_iroot(double v, int k) {
    if (v > 2147483647.0) v = 2147483647.0;
    if (k <= 1) return floor(v) > 1 ? floor(v) : 1;
    double lo = 1, hi = k == 2 ? 46341 : 1291;
    while (lo < hi) {
        double mid = floor((lo + hi + 1) * 0.5), p = mid;
        for (int i = 1; i < k; i++) p *= mid;
        if (p <= v) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

/* Round to nearest and keep subnormals while slime computes: engines often switch on flush-to-zero, which
   would change results. The host's mode is restored afterwards. Applies to the calling thread. */
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <xmmintrin.h>
typedef unsigned sl_fpmode;
static inline sl_fpmode sl_fp_enter(void) {
    sl_fpmode old = _mm_getcsr();
    if (old != 0x1f80u) _mm_setcsr(0x1f80u);   /* all exceptions masked, nearest, no FTZ or DAZ */
    return old;
}
static inline void sl_fp_leave(sl_fpmode old) { if (old != 0x1f80u) _mm_setcsr(old); }
#elif defined(__aarch64__) && defined(__GNUC__)
typedef uint64_t sl_fpmode;
#define SL_FPCR_CLEAR ((3ull << 22) | (1ull << 24) | (1ull << 19) | 3ull)   /* rounding, FZ, FZ16, AH and FIZ */
static inline sl_fpmode sl_fp_enter(void) {
    uint64_t old;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(old));
    if (old & SL_FPCR_CLEAR) { uint64_t mode = old & ~SL_FPCR_CLEAR; __asm__ __volatile__("msr fpcr, %0" : : "r"(mode)); }
    return old;
}
static inline void sl_fp_leave(sl_fpmode old) { if (old & SL_FPCR_CLEAR) __asm__ __volatile__("msr fpcr, %0" : : "r"(old)); }
#elif defined(_M_ARM64)
#include <intrin.h>
typedef unsigned __int64 sl_fpmode;
#define SL_FPCR_CLEAR ((3ull << 22) | (1ull << 24) | (1ull << 19) | 3ull)
static inline sl_fpmode sl_fp_enter(void) {
    sl_fpmode old = _ReadStatusReg(ARM64_FPCR);
    if (old & SL_FPCR_CLEAR) _WriteStatusReg(ARM64_FPCR, old & ~SL_FPCR_CLEAR);
    return old;
}
static inline void sl_fp_leave(sl_fpmode old) { if (old & SL_FPCR_CLEAR) _WriteStatusReg(ARM64_FPCR, old); }
#else
/* WebAssembly always rounds to nearest with subnormals; other targets get no special handling. */
typedef int sl_fpmode;
static inline sl_fpmode sl_fp_enter(void) { return 0; }
static inline void sl_fp_leave(sl_fpmode old) { (void)old; }
#endif

#endif
