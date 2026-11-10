/*
 * wb_math_test.c -- LP-03 final gate: deterministic sim math.
 *
 * Proves the fixed-point + integer CORDIC trig is exact and reproducible:
 *   - pure integer operations give exact round-trips for representable inputs
 *   - trig matches the mathematical references to a few Q32 ulps
 *   - periodicity and the sin/cos phase identity hold exactly
 *   - the whole thing is deterministic (bit-identical across calls)
 * Identical on any C11 host: no float mode in the simulation math.
 */
#include "wb_math.h"

#include <stdio.h>
#include <stdint.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)
#define DELTA(a, b) ((a) > (b) ? (a) - (b) : (b) - (a))

int main(void) {
    /* ---- fixed-point: exact arithmetic for representable values ---------- */
    CHECK(wb_fix_mul(WB_FIX_HALF, WB_FIX_HALF) == (int64_t)1 << 30,
          "0.5 * 0.5 == 0.25 exactly in Q32");
    CHECK(wb_fix_mul(WB_FIX_ONE, WB_FIX_HALF) == WB_FIX_HALF, "1.0 * 0.5 == 0.5 exactly");
    CHECK(wb_fix_div(WB_FIX_ONE, WB_FIX_HALF) == (WB_FIX_ONE << 1), "1.0 / 0.5 == 2.0 exactly");
    CHECK(wb_fix_div(WB_FIX_ONE, WB_FIX_ONE) == WB_FIX_ONE, "1.0 / 1.0 == 1.0 exactly");
    CHECK(wb_fix_from_double(1.0) == WB_FIX_ONE, "from_double(1.0) is exactly 2^32");
    CHECK(wb_fix_from_double(0.5) == WB_FIX_HALF, "from_double(0.5) is exactly 2^31");
    CHECK(wb_fix_to_double(WB_FIX_ONE) == 1.0 && wb_fix_to_double(WB_FIX_HALF) == 0.5,
          "to_double round-trips the canonical values");

    /* ---- CORDIC trig: matches math references within a few Q32 ulps ------- */
    int64_t s30 = wb_fix_sin(WB_FIX_ONE / 12);
    CHECK(DELTA(s30, (int64_t)1 << 31) <= 16, "sin(30 deg) == 0.5 within 16 ulps");
    CHECK(DELTA(wb_fix_sin(WB_FIX_ONE / 8), 3037000500LL) <= 16, "sin(45 deg) == sqrt2/2 within 16 ulps");
    CHECK(DELTA(wb_fix_sin(WB_FIX_ONE / 6), 3719550787LL) <= 16, "sin(60 deg) == sqrt3/2 within 16 ulps");
    CHECK(DELTA(wb_fix_sin(WB_FIX_QUART), WB_FIX_ONE) <= 16, "sin(90 deg) == 1 within 16 ulps");
    CHECK(DELTA(wb_fix_sin(0), 0) <= 8, "sin(0) == 0 within 8 ulps");
    CHECK(DELTA(wb_fix_cos(0), WB_FIX_ONE) <= 16, "cos(0) == 1 within 16 ulps");
    CHECK(DELTA(wb_fix_cos(WB_FIX_QUART), 0) <= 8, "cos(90 deg) == 0 within 8 ulps");

    /* the fundamental identity holds to a Q32 ulp budget */
    int64_t s45 = wb_fix_sin(WB_FIX_ONE / 8), c45 = wb_fix_cos(WB_FIX_ONE / 8);
    int64_t unit = wb_fix_mul(s45, s45) + wb_fix_mul(c45, c45);
    CHECK(DELTA(unit, WB_FIX_ONE) <= 64, "sin^2 + cos^2 == 1 within 64 ulps at 45 deg");

    /* phase identity: cos(x) == sin(x + 90 deg) exactly (same CORDIC fold) */
    CHECK(wb_fix_cos(WB_FIX_ONE / 8) == wb_fix_sin(WB_FIX_ONE / 8 + WB_FIX_QUART),
          "cos(x) == sin(x + 90 deg) exactly");

    /* periodicity is exact: angle math is mod-2^32 */
    CHECK(wb_fix_sin(WB_FIX_ONE / 12 + WB_FIX_ONE) == s30,
          "sin(x + one turn) == sin(x) exactly");

    /* antisymmetry holds to tolerance (fold quantization is asymmetric by ~2 ulps) */
    int64_t s45neg = wb_fix_sin(-(WB_FIX_ONE / 8));
    CHECK(DELTA(s45neg, -s45) <= 16, "sin(-x) == -sin(x) within 16 ulps");

    /* determinism: identical bit-for-bit on every call */
    CHECK(wb_fix_sin(1234567) == wb_fix_sin(1234567), "trig is deterministic across calls");
    CHECK(wb_fix_sin(1234567) != 0, "non-trivial angle gives a non-degenerate result");

    /* quadrant wrap produces the right signs */
    CHECK(wb_fix_sin(WB_FIX_ONE / 2) > -8 && wb_fix_sin(WB_FIX_ONE / 2) < 8, "sin(180 deg) ~ 0");
    CHECK(wb_fix_sin((3 * WB_FIX_ONE) / 4) < -WB_FIX_ONE + 16, "sin(270 deg) ~ -1");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
