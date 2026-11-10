/*
 * wb_fastmath.h -- fast branchless trig for the throughput path (LP-08 seed).
 *
 * Slermed from Silas Lock's folded-sine polynomial as the user implemented it
 * in wubuwizard (wubu_foldmath.h -- "The Folded Polynomial", N64 2023): define
 * ONE small polynomial on [0, pi/4] and cover the whole circle with the wave
 * symmetries. No table, no sqrt, no libm; the few FMA/polynomial ops vectorize
 * to SIMD width, so a sin/cos loop stops riding libm's scalar __svml path.
 *
 * This is the COMPUTE arm for render/gameplay. The integer CORDIC in wb_math
 * remains the deterministic-authority path for the sim spine; float RNG and
 * libm trig are both out of the reproducible path, which is why they live here.
 *
 * fp32 accuracy: < ~5e-7 to libm over normal game ranges (Cody-Waite two-part
 * reduction is accurate to |x| < ~1e4 and degrades gracefully beyond).
 */
#ifndef WB_FASTMATH_H
#define WB_FASTMATH_H

#include <math.h>

#if defined(__AVX2__) && defined(__FMA__)
#include <immintrin.h>
#endif

/* Taylor coefficients: sin[r] odd to r^9, cos[r] even to r^8 on [-pi/4, pi/4] */
#define WBF_S1  1.0f
#define WBF_S3 -0.16666666666666667f
#define WBF_S5  0.008333333333333333f
#define WBF_S7 -0.0001984126984126984f
#define WBF_S9  0.0000027557319223985893f
#define WBF_C2 -0.5f
#define WBF_C4  0.041666666666666664f
#define WBF_C6 -0.001388888888888889f
#define WBF_C8  0.000024801587301587305f

/* two-part pi/2 and 2/pi for the Cody-Waite reduction */
#define WBF_PI2_HI 1.57079637050628662109375f
#define WBF_PI2_LO -4.37113900018624283e-8f
#define WBF_2_PI   0.6366197723675814f

static inline float wb_fast_sin_poly(float r) {
    float r2 = r * r;
    return r * (WBF_S1 + r2 * (WBF_S3 + r2 * (WBF_S5 + r2 * (WBF_S7 + r2 * WBF_S9))));
}
static inline float wb_fast_cos_poly(float r) {
    float r2 = r * r;
    return 1.0f + r2 * (WBF_C2 + r2 * (WBF_C4 + r2 * (WBF_C6 + r2 * WBF_C8)));
}

/* both sin and cos of x in one reduction. Branchless(float-mask) selects so
 * the whole fold stays SIMD-width. */
static inline void wb_fast_sincos(float x, float *s, float *c) {
    float nf = roundf(x * WBF_2_PI);              /* nearest multiple of pi/2 */
    float r  = fmaf(-nf, WBF_PI2_HI, x);          /* Cody-Waite two-part     */
    r        = fmaf(-nf, WBF_PI2_LO, r);
    float vs = wb_fast_sin_poly(r);
    float vc = wb_fast_cos_poly(r);
    float q  = nf - 4.0f * floorf(nf * 0.25f);    /* quadrant in [0,4)       */
    float odd   = ((q >= 1.0f) & (q < 2.0f)) | (q >= 3.0f) ? 1.0f : 0.0f;
    float s_mag = fmaf(odd, vc - vs, vs);         /* odd ? vc : vs (swap)    */
    float c_mag = fmaf(odd, vs - vc, vc);
    float s_neg = (q >= 2.0f) ? -1.0f : 1.0f;
    float c_neg = (q >= 1.0f) & (q < 3.0f) ? -1.0f : 1.0f;
    *s = s_mag * s_neg;
    *c = c_mag * c_neg;
}

static inline float wb_fast_sin(float x) { float s, c; wb_fast_sincos(x, &s, &c); return s; }
static inline float wb_fast_cos(float x) { float s, c; wb_fast_sincos(x, &s, &c); return c; }

/* ---- AVX2 8-wide batch: one fold per 8 lanes, no branch, FMA everywhere. */
#if defined(__AVX2__) && defined(__FMA__)
static inline void wb_fast_sincos8(const float *x, float *out_s, float *out_c, int n) {
    const __m256 k2opi = _mm256_set1_ps(WBF_2_PI), khalf = _mm256_set1_ps(WBF_PI2_HI),
                 klo = _mm256_set1_ps(WBF_PI2_LO);
    const __m256 s1=_mm256_set1_ps(WBF_S1),s3=_mm256_set1_ps(WBF_S3),s5=_mm256_set1_ps(WBF_S5),
                 s7=_mm256_set1_ps(WBF_S7),s9=_mm256_set1_ps(WBF_S9);
    const __m256 c2=_mm256_set1_ps(WBF_C2),c4=_mm256_set1_ps(WBF_C4),c6=_mm256_set1_ps(WBF_C6),
                 c8=_mm256_set1_ps(WBF_C8);
    const __m256 kone=_mm256_set1_ps(1.0f), kfour=_mm256_set1_ps(4.0f), khalf4=_mm256_set1_ps(0.25f);
    int i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 xv = _mm256_loadu_ps(x + i);
        __m256 nf = _mm256_round_ps(_mm256_mul_ps(xv, k2opi),
                                    _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
        __m256 r = _mm256_fnmadd_ps(nf, khalf, xv);
        r = _mm256_fnmadd_ps(nf, klo, r);
        __m256 q = _mm256_sub_ps(nf, _mm256_mul_ps(kfour, _mm256_floor_ps(_mm256_mul_ps(nf, khalf4))));
        __m256 ar = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), r);   /* |r| */
        __m256 r2 = _mm256_mul_ps(ar, ar);
        __m256 ps = _mm256_fmadd_ps(s9, r2, s7);
        ps = _mm256_fmadd_ps(ps, r2, s5); ps = _mm256_fmadd_ps(ps, r2, s3);
        ps = _mm256_fmadd_ps(ps, r2, s1);
        __m256 sa = _mm256_mul_ps(ar, ps);
        __m256 pc = _mm256_fmadd_ps(c8, r2, c6);
        pc = _mm256_fmadd_ps(pc, r2, c4); pc = _mm256_fmadd_ps(pc, r2, c2);
        __m256 ca = _mm256_fmadd_ps(pc, r2, kone);
        __m256 sgn = _mm256_blendv_ps(kone, _mm256_set1_ps(-1.0f),
                                      _mm256_cmp_ps(r, _mm256_setzero_ps(), _CMP_LT_OQ));
        __m256 sa_sgn = _mm256_mul_ps(sa, sgn);
        __m256 sa_neg = _mm256_xor_ps(sa_sgn, _mm256_set1_ps(-0.0f));
        __m256 ca_neg = _mm256_xor_ps(ca, _mm256_set1_ps(-0.0f));
        __m256 m1 = _mm256_cmp_ps(q, _mm256_set1_ps(1.0f), _CMP_LT_OQ);
        __m256 m2 = _mm256_cmp_ps(q, _mm256_set1_ps(2.0f), _CMP_LT_OQ);
        __m256 m3 = _mm256_cmp_ps(q, _mm256_set1_ps(3.0f), _CMP_LT_OQ);
        __m256 m4 = _mm256_cmp_ps(q, _mm256_set1_ps(3.0f), _CMP_GE_OQ);
        __m256 m2b = _mm256_andnot_ps(m1, m2), m3b = _mm256_andnot_ps(m2, m3);
        __m256 sv = _mm256_blendv_ps(_mm256_setzero_ps(), sa_sgn, m1);
        sv = _mm256_blendv_ps(sv, ca, m2b);
        sv = _mm256_blendv_ps(sv, sa_neg, m3b);
        sv = _mm256_blendv_ps(sv, ca_neg, m4);
        __m256 cv = _mm256_blendv_ps(_mm256_setzero_ps(), ca, m1);
        cv = _mm256_blendv_ps(cv, sa_neg, m2b);
        cv = _mm256_blendv_ps(cv, ca_neg, m3b);
        cv = _mm256_blendv_ps(cv, sa_sgn, m4);
        _mm256_storeu_ps(out_s + i, sv);
        _mm256_storeu_ps(out_c + i, cv);
    }
    for (; i < n; i++) { float s, c; wb_fast_sincos(x[i], &s, &c); out_s[i] = s; out_c[i] = c; }
}
#else
static inline void wb_fast_sincos8(const float *x, float *out_s, float *out_c, int n) {
    for (int i = 0; i < n; i++) { float s, c; wb_fast_sincos(x[i], &s, &c); out_s[i] = s; out_c[i] = c; }
}
#endif

#endif /* WB_FASTMATH_H */
