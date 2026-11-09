/*
 * wb_spawn_test.c -- slice 19 gate: spawn points.
 *
 * Spawn points are data, not code: position, team, tag, gametype. This proves
 * selection respects team and gametype, prefers unoccupied points, is
 * deterministic, and reports exhaustion rather than handing out a stale spot.
 */
#include "wb_spawn.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main(void) {
    wb_spawn_set *s = wb_spawn_new();
    CHECK(s != NULL, "spawn set created");

    const float red[3]  = { 10.0f, 0.0f, 0.0f };
    const float blue[3] = { -10.0f, 0.0f, 0.0f };
    const float red2[3] = { 12.0f, 0.0f, 0.0f };
    const float pits[3] = { 5.0f, 0.0f, 5.0f };
    const float mid[3]  = { 0.0f, 0.0f, 0.0f };

    /* ---- add + count ----------------------------------------------------- */
    int r1 = wb_spawn_add(s, red,  0, "red-base",   "zone-wars");
    int b1 = wb_spawn_add(s, blue, 1, "blue-base",  "zone-wars");
    int r2 = wb_spawn_add(s, red2, 0, "red-base-2", "zone-wars");
    int p1 = wb_spawn_add(s, pits, 0, "pits",       "rush-hour");
    int m1 = wb_spawn_add(s, mid, -1, "mid",        "");
    CHECK(r1 == 0 && b1 == 1 && r2 == 2 && p1 == 3 && m1 == 4,
          "indices are assigned in order");
    CHECK(wb_spawn_count(s) == 5, "count is 5");

    const wb_spawn_point *p = wb_spawn_get(s, 0);
    CHECK(p && p->pos[0] == 10.0f && p->team == 0, "point 0 keeps its position and team");
    CHECK(p && !strcmp(p->tag, "red-base"), "point 0 keeps its tag");
    CHECK(p && !strcmp(p->gamemode, "zone-wars"), "point 0 keeps its gamemode");
    CHECK(p && p->occupied == 0, "a new point is unoccupied");
    const wb_spawn_point *u = wb_spawn_get(s, 4);
    CHECK(u && u->team == -1 && u->gamemode[0] == 0,
          "the untagged point is any-team, any-gametype");
    CHECK(wb_spawn_get(s, -1) == NULL && wb_spawn_get(s, 99) == NULL,
          "out-of-range lookups return NULL");

    /* ---- availability filters --------------------------------------------- */
    CHECK(wb_spawn_available(s, "zone-wars", 0) == 3,
          "zone-wars team 0 sees red, red-2 and the untagged mid");
    CHECK(wb_spawn_available(s, "zone-wars", 1) == 2,
          "zone-wars team 1 sees blue and the untagged mid");
    CHECK(wb_spawn_available(s, "rush-hour", 0) == 2,
          "rush-hour team 0 sees the pits and the untagged mid");
    CHECK(wb_spawn_available(s, "other-mode", 0) == 1,
          "a foreign gametype only sees the untagged point");

    /* ---- picking ----------------------------------------------------------- */
    float out[3] = { -1.0f, -1.0f, -1.0f };
    int idx = wb_spawn_pick(s, "zone-wars", 0, out);
    CHECK(idx == 0, "team 0 picks the first red point");
    CHECK(out[0] == 10.0f && out[1] == 0.0f && out[2] == 0.0f,
          "the picked position is copied out");

    idx = wb_spawn_pick(s, "zone-wars", 1, out);
    CHECK(idx == 1, "team 1 picks the blue point, not red");
    CHECK(out[0] == -10.0f, "team 1 position is correct");

    idx = wb_spawn_pick(s, "rush-hour", 0, out);
    CHECK(idx == 3, "a different gametype picks its own point first");
    CHECK(out[0] == 5.0f && out[2] == 5.0f, "the pits position is correct");

    /* ---- occupancy --------------------------------------------------------- */
    wb_spawn_occupy(s, 0);
    CHECK(wb_spawn_get(s, 0)->occupied == 1, "occupy marks the point");
    CHECK(wb_spawn_available(s, "zone-wars", 0) == 2,
          "an occupied point is no longer available");
    idx = wb_spawn_pick(s, "zone-wars", 0, out);
    CHECK(idx == 2, "the next free red point is picked");
    CHECK(out[0] == 12.0f, "the replacement position is correct");

    wb_spawn_release(s, 0);
    CHECK(wb_spawn_get(s, 0)->occupied == 0, "release clears the point");
    CHECK(wb_spawn_available(s, "zone-wars", 0) == 3,
          "the released point is available again");

    /* ---- determinism ------------------------------------------------------- */
    int a = wb_spawn_pick(s, "zone-wars", 0, NULL);
    int b = wb_spawn_pick(s, "zone-wars", 0, NULL);
    CHECK(a == b, "picking is deterministic (ties break to the lowest index)");
    CHECK(a == 0, "the first free point wins");

    /* ---- exhaustion -------------------------------------------------------- */
    wb_spawn_occupy(s, 0);
    wb_spawn_occupy(s, 2);
    wb_spawn_occupy(s, 4);
    CHECK(wb_spawn_available(s, "zone-wars", 0) == 0,
          "every zone-wars team-0 point occupied -> none available");
    CHECK(wb_spawn_pick(s, "zone-wars", 0, out) == -1,
          "picking an exhausted team returns -1");
    /* team 1 still has its point */
    CHECK(wb_spawn_pick(s, "zone-wars", 1, out) == 1,
          "exhaustion is per team, not global");

    /* ---- empty set --------------------------------------------------------- */
    wb_spawn_set *e = wb_spawn_new();
    CHECK(wb_spawn_count(e) == 0, "a new set is empty");
    CHECK(wb_spawn_pick(e, "anything", 0, out) == -1,
          "picking from an empty set returns -1");
    CHECK(wb_spawn_available(e, "anything", 0) == 0,
          "an empty set has nothing available");
    wb_spawn_free(e);

    /* ---- NULL tolerance ----------------------------------------------------- */
    wb_spawn_free(NULL);
    CHECK(wb_spawn_count(NULL) == 0, "a NULL set counts as empty");
    CHECK(wb_spawn_pick(NULL, "x", 0, out) == -1, "a NULL set picks nothing");
    CHECK(wb_spawn_get(NULL, 0) == NULL, "a NULL set has no points");
    CHECK(wb_spawn_add(NULL, red, 0, "x", "y") == -1, "adding to a NULL set fails");
    wb_spawn_occupy(NULL, 0);
    wb_spawn_release(NULL, 0);

    wb_spawn_free(s);
    printf("\n=== SPAWN TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
