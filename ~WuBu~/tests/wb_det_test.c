/*
 * wb_det_test.c -- LP-03 stone 1 gate: the deterministic substrate.
 *
 * Proves the PRNG is a fixed, reproducible stream (same seed -> same
 * sequence, pinned against splitmix64 reference values) and the checksum is
 * order-dependent and repeatable -- the two things a replay/rollback depends
 * on. Both are pure 64-bit arithmetic, so passing here means identical results
 * on any host.
 */
#include "wb_det.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main(void) {
    /* ---- PRNG determinism: same seed, same 1000 values ------------------ */
    wb_det_rng a, b;
    wb_det_rng_seed(&a, 12345), wb_det_rng_seed(&b, 12345);
    int identical = 1;
    for (int i = 0; i < 1000; i++)
        if (wb_det_rng_next(&a) != wb_det_rng_next(&b)) { identical = 0; break; }
    CHECK(identical, "same seed reproduces the same 1000-value stream");

    /* ---- golden pin: splitmix64 reference values ------------------------- */
    wb_det_rng_seed(&a, 0);
    CHECK(wb_det_rng_next(&a) == 0xe220a8397b1dcdafULL,
          "splitmix64(seed 0) first value is the reference");
    wb_det_rng_seed(&a, 42);
    CHECK(wb_det_rng_next(&a) == 0xbdd732262feb6e95ULL,
          "splitmix64(seed 42) first value is the reference");
    wb_det_rng_seed(&a, 7);
    const uint64_t expect[4] = {
        0x63cbe1e459320dd7ULL, 0x044c3cd7f43c661cULL,
        0xe6984080bab12a02ULL, 0x953aeb70673e29cbULL };
    int seq_ok = 1;
    for (int i = 0; i < 4; i++)
        if (wb_det_rng_next(&a) != expect[i]) { seq_ok = 0; break; }
    CHECK(seq_ok, "splitmix64(seed 7) first four values match the reference");

    /* ---- range + f01 stay in bounds --------------------------------------- */
    int inb = 1;
    wb_det_rng_seed(&a, 99);
    for (int i = 0; i < 5000; i++) {
        uint64_t rv = wb_det_rng_range(&a, 1000);
        double f = wb_det_rng_f01(&a);
        if (rv >= 1000 || f < 0.0 || f >= 1.0) { inb = 0; break; }
    }
    CHECK(inb, "range() and f01() stay in bounds over 5000 draws");

    /* ---- checksum: repeatable and order-dependent ------------------------- */
    uint8_t abc[3] = {1, 2, 3}, cba[3] = {3, 2, 1};
    uint64_t s1 = wb_det_checksum(abc, sizeof abc);
    uint64_t s2 = wb_det_checksum(abc, sizeof abc);
    uint64_t s3 = wb_det_checksum(cba, sizeof cba);
    CHECK(s1 == s2 && s1 != 0, "checksum is repeatable for identical bytes");
    CHECK(s1 != s3, "checksum is order-dependent (1,2,3 vs 3,2,1 differ)");

    /* incremental stream equals one-shot over the concatenation */
    uint64_t inc = wb_det_checksum_stream(WB_DET_FNV_OFFSET, "foo", 3);
    inc = wb_det_checksum_stream(inc, "bar", 3);
    CHECK(inc == wb_det_checksum("foobar", 6), "incremental stream == one-shot a||b");

    /* empty input hashes to the offset basis, deterministic */
    CHECK(wb_det_checksum(NULL, 0) == WB_DET_FNV_OFFSET, "empty checksum is the FNV offset");

    /* ---- fixed timestep: exact at 64Hz, deterministic, carries a remainder ---- */
    wb_det_fixed f;
    wb_det_fixed_init(&f, 1.0 / 64.0);
    int steps = wb_det_fixed_advance(&f, 1.0);
    CHECK(steps == 64, "one second at 64Hz is exactly 64 simulation steps");
    CHECK(wb_det_fixed_remainder(&f) == 0.0, "64Hz carries no remainder (exact delta time)");

    wb_det_fixed g, hh;
    wb_det_fixed_init(&g, 1.0 / 64.0);
    wb_det_fixed_init(&hh, 1.0 / 64.0);
    const double partials[5] = {0.016, 0.017, 0.015, 0.032, 0.020};
    long total_g = 0, total_h = 0;
    for (int i = 0; i < 5; i++) { total_g += wb_det_fixed_advance(&g, partials[i]); }
    for (int i = 0; i < 5; i++) { total_h += wb_det_fixed_advance(&hh, partials[i]); }
    CHECK(total_g == total_h, "identical dt sequences step identically (deterministic)");
    CHECK(wb_det_fixed_remainder(&g) == wb_det_fixed_remainder(&hh),
          "remainder is identical for identical dt sequences");
    CHECK(total_g > 0 && total_g <= 7 && wb_det_fixed_remainder(&g) >= 0.0 && wb_det_fixed_remainder(&g) < (1.0 / 64.0),
          "plausible step count and remainder in [0, step)");

    /* partial steps accumulate to exactly one whole step at 64Hz */
    wb_det_fixed_init(&f, 1.0 / 64.0);
    int gathered = 0;
    for (int i = 0; i < 64; i++) gathered += wb_det_fixed_advance(&f, 1.0 / 64.0);
    CHECK(gathered == 64 && wb_det_fixed_remainder(&f) == 0.0,
          "64 exact partials gather into 64 steps with no drift");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
