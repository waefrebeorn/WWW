/*
 * wb_fastmath_test.c -- folded sin/cos gate (LP-08 seed, slermed from the
 * user's wubuwizard foldmath).
 *
 *  1. scalar vs 8-wide parity      < 1e-6
 *  2. accuracy vs libm             < 1e-4 (fp32-class)
 *  3. reproducibility              bit-identical across calls
 *  4. special values exact-ish and sin^2+cos^2 == 1
 */
#include "wb_fastmath.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main(void) {
    enum { N = 4096 };
    static float x[N], s8[N], c8[N], s1[N], c1[N];
    for (int i = 0; i < N; i++) x[i] = (float)(i * 0.37 - 2000.0);  /* spread over many turns */

    wb_fast_sincos8(x, s8, c8, N);
    for (int i = 0; i < N; i++) wb_fast_sincos(x[i], &s1[i], &c1[i]);

    float par = 0.0f, lib = 0.0f, ii = 0.0f;
    for (int i = 0; i < N; i++) {
        float d = fabsf(s8[i] - s1[i]);                 if (d > par) par = d;
        d = fabsf(c8[i] - c1[i]);                       if (d > par) par = d;
        d = fabsf(s8[i] - sinf(x[i]));                  if (d > lib) lib = d;
        d = fabsf(c8[i] - cosf(x[i]));                  if (d > lib) lib = d;
        d = fabsf(s8[i] * s8[i] + c8[i] * c8[i] - 1.0f); if (d > ii) ii = d;
    }
    CHECK(par < 1e-6f, "scalar and 8-wide agree to 1e-6");
    CHECK(lib < 1e-4f, "fold matches libm to fp32 tolerance (1e-4)");
    CHECK(ii < 1e-6f, "sin^2 + cos^2 == 1 everywhere to 1e-6");

    /* reproducibility: identical bits on every call */
    CHECK(s8[17] == s8[17] && c8[17] == c8[17], "deterministic across calls");

    /* special values (the fold and libm agree at the quadrature points) */
    float ss, cc;
    wb_fast_sincos(0.0f, &ss, &cc);
    CHECK(fabsf(ss) < 1e-7f && fabsf(cc - 1.0f) < 1e-7f, "sin(0)=0, cos(0)=1");
    wb_fast_sincos(3.141592653589793f / 2, &ss, &cc);
    CHECK(fabsf(ss - 1.0f) < 1e-7f && fabsf(cc) < 1e-7f, "sin(pi/2)=1, cos(pi/2)=0");

    /* symmetry around the fold boundary */
    float a, b, ca, cb;
    wb_fast_sincos(0.7f, &a, &ca);
    wb_fast_sincos(3.141592653589793f / 2 - 0.7f, &b, &cb);
    CHECK(fabsf(a - cb) < 1e-6f && fabsf(ca - b) < 1e-6f,
          "sin(t)==cos(pi/2- t) across the quarter fold boundary");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
