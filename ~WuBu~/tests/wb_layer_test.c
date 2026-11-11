/*
 * wb_layer_test.c -- LP-106 stone 1 gate: per-level capacity and layering.
 *
 * Devil's-advocate cases: a party larger than a whole layer is refused rather than
 * split; a party already on a layer STAYS there (co-location), so rejoining is not
 * an error; PACK fills the fullest layer that fits while SPREAD fills the emptiest;
 * a party that grows past its layer's remaining room is refused rather than
 * migrating; and a layer is never opened past the ceiling.
 */
#include "wb_layer.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define CEIL 3
#define LAYER_CAP 10

static int build(wb_layer_pool *p, uint32_t mode) {
    return wb_layer_init(p, 8, CEIL, LAYER_CAP, mode);
}

int main(void) {
    wb_layer_pool p;
    CHECK(build(&p, WB_LAYER_PACK) == 0, "the pool is created");
    CHECK(wb_layer_count(&p) == 0, "no layers exist yet");

    /* ---- spawning respects the ceiling ---- */
    CHECK(wb_layer_spawn(&p) == 0 && wb_layer_count(&p) == 1, "the first layer opens");
    CHECK(wb_layer_spawn(&p) == 0 && wb_layer_count(&p) == 2, "the second layer opens");
    CHECK(wb_layer_spawn(&p) == 0 && wb_layer_count(&p) == 3, "the third layer opens");
    CHECK(wb_layer_spawn(&p) == -1, "a fourth layer is refused at the ceiling");
    CHECK(wb_layer_count(&p) == 3, "and the count is unchanged");

    /* ---- PACK fills the fullest layer that has room ---- */
    wb_layer_result r = wb_layer_join(&p, 1, 4);
    CHECK(r.reason == WB_LAYER_OK && r.layer == 1, "party 1 joins layer 1");
    CHECK(wb_layer_used(&p, 1) == 4 && wb_layer_free_space(&p, 1) == 6, "layer 1 has 4 used");
    r = wb_layer_join(&p, 2, 4);
    CHECK(r.reason == WB_LAYER_OK && r.layer == 1, "party 2 packs into the fuller layer 1");
    CHECK(wb_layer_used(&p, 1) == 8, "layer 1 is now nearly full");
    r = wb_layer_join(&p, 3, 4);
    CHECK(r.reason == WB_LAYER_OK && r.layer == 2, "party 3 goes to layer 2 (layer 1 cannot fit it)");
    CHECK(wb_layer_used(&p, 2) == 4, "layer 2 has 4 used");

    /* ---- co-location: a party that rejoins stays put ---- */
    r = wb_layer_join(&p, 1, 2);
    CHECK(r.reason == WB_LAYER_OK && r.layer == 1, "party 1 rejoins its own layer");
    CHECK(wb_layer_used(&p, 1) == 6, "shrinking 4 to 2 frees two slots (8 - 4 + 2)");
    CHECK(wb_layer_free_space(&p, 1) == 4, "layer 1 has four slots free");
    CHECK(wb_layer_contains(&p, 1, 1) == 1, "party 1 is on layer 1");
    CHECK(wb_layer_contains(&p, 2, 1) == 0, "party 1 is not on layer 2");
    uint64_t found = 0;
    CHECK(wb_layer_find(&p, 1, &found) == 1 && found == 1, "find locates the party's layer");
    CHECK(wb_layer_find(&p, 999, &found) == 0, "an unknown party is on no layer");

    /* ---- a party that grows past its layer's room is refused, not migrated ---- */
    r = wb_layer_join(&p, 1, 7);
    CHECK(r.reason == WB_LAYER_REFUSED_CEILING && r.layer == 0,
          "party 1 cannot grow by five into four free slots");
    CHECK(wb_layer_used(&p, 1) == 6, "and the refusal changed nothing");
    r = wb_layer_join(&p, 1, 6);
    CHECK(r.reason == WB_LAYER_OK && r.layer == 1, "growing by exactly the free space fits");
    CHECK(wb_layer_used(&p, 1) == 10, "and fills the layer");

    /* ---- leaving frees the room ---- */
    CHECK(wb_layer_leave(&p, 1) == 0, "party 1 leaves");
    CHECK(wb_layer_used(&p, 1) == 4, "the room it held is freed");
    CHECK(wb_layer_contains(&p, 1, 1) == 0, "and the party is gone");
    CHECK(wb_layer_leave(&p, 1) == -1, "leaving twice is refused");
    CHECK(wb_layer_leave(&p, 999) == -1, "leaving a party on no layer is refused");
    r = wb_layer_join(&p, 4, 6);
    CHECK(r.reason == WB_LAYER_OK && r.layer == 1, "the freed room is reusable");

    /* ---- a party bigger than a whole layer is refused ---- */
    r = wb_layer_join(&p, 5, LAYER_CAP + 1);
    CHECK(r.reason == WB_LAYER_REFUSED_TOO_BIG && r.layer == 0,
          "an oversized party is refused rather than split");
    r = wb_layer_join(&p, 5, LAYER_CAP);
    CHECK(r.reason == WB_LAYER_OK, "a party exactly the layer size fits");

    /* ---- the ceiling turns a full world away ---- */
    wb_layer_pool q;
    CHECK(wb_layer_init(&q, 4, 1, LAYER_CAP, WB_LAYER_PACK) == 0, "a one-layer pool is created");
    wb_layer_spawn(&q);
    r = wb_layer_join(&q, 1, LAYER_CAP);
    CHECK(r.reason == WB_LAYER_OK, "the only layer fills");
    r = wb_layer_join(&q, 2, 1);
    CHECK(r.reason == WB_LAYER_REFUSED_CEILING && r.layer == 0,
          "with the ceiling reached, a full world refuses");

    /* ---- SPREAD fills the emptiest layer that has room ---- */
    wb_layer_pool s;
    CHECK(build(&s, WB_LAYER_SPREAD) == 0, "a spread pool is created");
    wb_layer_spawn(&s);
    wb_layer_spawn(&s);
    wb_layer_spawn(&s);
    r = wb_layer_join(&s, 1, 4);
    CHECK(r.reason == WB_LAYER_OK && r.layer == 1, "party 1 joins layer 1");
    r = wb_layer_join(&s, 2, 4);
    CHECK(r.reason == WB_LAYER_OK && r.layer == 2, "party 2 spreads to the emptiest layer");
    r = wb_layer_join(&s, 3, 4);
    CHECK(r.reason == WB_LAYER_OK && r.layer == 3, "party 3 spreads again");
    r = wb_layer_join(&s, 4, 4);
    CHECK(r.reason == WB_LAYER_OK && r.layer == 1, "party 4 wraps back to layer 1");
    CHECK(wb_layer_used(&s, 1) == 8 && wb_layer_used(&s, 2) == 4 && wb_layer_used(&s, 3) == 4,
          "the spread loads are even");

    /* ---- clear ---- */
    wb_layer_clear(&p);
    CHECK(wb_layer_count(&p) == 0, "clear closes every layer");
    CHECK(wb_layer_used(&p, 1) == 0, "a cleared pool has no used capacity");
    r = wb_layer_join(&p, 1, 1);
    CHECK(r.reason == WB_LAYER_OK && r.layer == 1, "a cleared pool opens a fresh layer");

    /* ---- determinism ---- */
    wb_layer_pool x, y;
    CHECK(build(&x, WB_LAYER_PACK) == 0 && build(&y, WB_LAYER_PACK) == 0, "two pools are built");
    const uint64_t parties[] = { 1, 2, 3, 4, 5, 6 };
    const uint32_t sizes[] = { 4, 4, 4, 2, 6, 3 };
    int same = 1;
    for (uint32_t i = 0; i < 6; i++) {
        wb_layer_result rx = wb_layer_join(&x, parties[i], sizes[i]);
        wb_layer_result ry = wb_layer_join(&y, parties[i], sizes[i]);
        same &= (rx.reason == ry.reason && rx.layer == ry.layer);
    }
    CHECK(same, "two identical pools layer identically");
    same = 1;
    for (uint64_t l = 1; l <= CEIL; l++)
        same &= (wb_layer_used(&x, l) == wb_layer_used(&y, l));
    CHECK(same, "and end with identical loads");

    /* ---- degenerate input ---- */
    CHECK(wb_layer_init(NULL, 4, 2, 10, WB_LAYER_PACK) == -1, "a NULL pool is rejected");
    CHECK(wb_layer_init(&x, 0, 2, 10, WB_LAYER_PACK) == -1, "a zero capacity is rejected");
    CHECK(wb_layer_init(&x, 4, 2, 0, WB_LAYER_PACK) == -1, "a zero layer capacity is rejected");
    CHECK(wb_layer_spawn(NULL) == -1, "a NULL pool refuses to spawn");
    r = wb_layer_join(NULL, 1, 1);
    CHECK(r.layer == 0 && r.reason == WB_LAYER_REFUSED_CEILING, "a NULL pool refuses to join");
    r = wb_layer_join(&x, 0, 1);
    CHECK(r.layer == 0, "party id 0 is refused");
    r = wb_layer_join(&x, 1, 0);
    CHECK(r.layer == 0, "a zero-size party is refused");
    CHECK(wb_layer_leave(NULL, 1) == -1, "a NULL pool refuses to leave");
    CHECK(wb_layer_count(NULL) == 0, "a NULL pool has no layers");
    CHECK(wb_layer_used(NULL, 1) == 0, "a NULL pool has no used capacity");
    CHECK(wb_layer_free_space(NULL, 1) == 0, "a NULL pool has no free capacity");
    CHECK(wb_layer_find(NULL, 1, &found) == 0, "a NULL pool finds no party");
    CHECK(wb_layer_contains(NULL, 1, 1) == 0, "a NULL pool contains nothing");
    CHECK(wb_layer_used(&x, 999) == 0, "an unknown layer has no used capacity");
    CHECK(wb_layer_free_space(&x, 999) == 0, "an unknown layer has no free capacity");
    CHECK(wb_layer_contains(&x, 999, 1) == 0, "an unknown layer holds no one");

    wb_layer_free(&p);
    wb_layer_free(&q);
    wb_layer_free(&s);
    wb_layer_free(&x);
    wb_layer_free(&y);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
