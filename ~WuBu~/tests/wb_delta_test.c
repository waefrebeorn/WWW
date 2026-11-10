/*
 * wb_delta_test.c -- LP-28 gate: delta compression.
 *
 * Devil's-advocate cases: a few changed words compress well, a no-op delta is
 * just the bitmap, a fully changed state still round-trips, lengths that are not
 * a multiple of four (padding), determinism, and capacity validation.
 */
#include "wb_delta.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define LEN 64
static uint8_t g_old[LEN], g_new[LEN], g_delta[LEN * 2], g_out[LEN];

static void init_state(void) {
    for (int i = 0; i < LEN; i++) { g_old[i] = (uint8_t)i; g_new[i] = (uint8_t)i; }
}

int main(void) {
    /* ---- a few changed words compress well ---- */
    init_state();
    g_new[3] = 0xAA;                               /* word 0 */
    g_new[40] = 0x11;                              /* word 10 */
    size_t n = wb_delta_encode(g_old, g_new, LEN, g_delta, sizeof g_delta);
    CHECK(n > 0, "encoding succeeds");
    CHECK(n < LEN, "a sparse delta is smaller than the full state");
    CHECK(n == 2 + 2 * 4, "the delta is the bitmap plus the changed words");

    CHECK(wb_delta_apply(g_old, g_delta, LEN, g_out, sizeof g_out) == LEN, "apply succeeds");
    CHECK(memcmp(g_out, g_new, LEN) == 0, "the reconstructed state matches exactly");

    /* ---- a no-op delta is just the bitmap ---- */
    init_state();
    n = wb_delta_encode(g_old, g_new, LEN, g_delta, sizeof g_delta);
    CHECK(n == 2, "a no-op delta is only the bitmap");
    CHECK(wb_delta_apply(g_old, g_delta, LEN, g_out, sizeof g_out) == LEN, "no-op apply succeeds");
    CHECK(memcmp(g_out, g_new, LEN) == 0, "the state is unchanged");

    /* ---- a fully changed state round-trips ---- */
    init_state();
    for (int i = 0; i < LEN; i++) g_new[i] = (uint8_t)(i * 7 + 1);
    n = wb_delta_encode(g_old, g_new, LEN, g_delta, sizeof g_delta);
    CHECK(n == wb_delta_max_size(LEN), "a fully changed delta is the worst case");
    CHECK(wb_delta_apply(g_old, g_delta, LEN, g_out, sizeof g_out) == LEN, "full apply succeeds");
    CHECK(memcmp(g_out, g_new, LEN) == 0, "the fully changed state round-trips");

    /* ---- a length that is not a multiple of four ---- */
    init_state();
    g_new[5] = 0x5A;                               /* inside the final partial word */
    n = wb_delta_encode(g_old, g_new, LEN, g_delta, sizeof g_delta);
    CHECK(n == 2 + 4, "a partial final word is one changed word");
    CHECK(wb_delta_apply(g_old, g_delta, LEN, g_out, sizeof g_out) == LEN, "partial apply succeeds");
    CHECK(memcmp(g_out, g_new, LEN) == 0, "the partial word round-trips");

    /* ---- a very short state ---- */
    uint8_t tiny_old[1] = { 1 }, tiny_new[1] = { 2 }, tiny_delta[8], tiny_out[1];
    n = wb_delta_encode(tiny_old, tiny_new, 1, tiny_delta, sizeof tiny_delta);
    CHECK(n == 1 + 4, "a one-byte state uses one bitmap byte and one word");
    CHECK(wb_delta_apply(tiny_old, tiny_delta, 1, tiny_out, sizeof tiny_out) == 1, "tiny apply succeeds");
    CHECK(tiny_out[0] == 2, "the tiny state round-trips");

    /* ---- capacity validation ---- */
    init_state();
    g_new[0] = 0xFF;
    CHECK(wb_delta_encode(g_old, g_new, LEN, g_delta, 1) == 0, "an undersized encode buffer fails");
    CHECK(wb_delta_apply(g_old, g_delta, LEN, g_out, 1) == 0, "an undersized output buffer fails");
    CHECK(wb_delta_encode(NULL, g_new, LEN, g_delta, sizeof g_delta) == 0, "NULL old state is rejected");
    CHECK(wb_delta_encode(g_old, g_new, 0, g_delta, sizeof g_delta) == 0, "a zero length is rejected");

    /* ---- determinism ---- */
    init_state();
    g_new[7] = 0x77; g_new[30] = 0x33;
    uint8_t d1[LEN * 2], d2[LEN * 2];
    size_t n1 = wb_delta_encode(g_old, g_new, LEN, d1, sizeof d1);
    size_t n2 = wb_delta_encode(g_old, g_new, LEN, d2, sizeof d2);
    CHECK(n1 == n2 && memcmp(d1, d2, n1) == 0, "encoding is deterministic");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
