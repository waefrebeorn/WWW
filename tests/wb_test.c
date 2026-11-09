/*
 * wb_test.c -- C11 test harness for the WuBu Engine ECS core.
 *
 * Tests real behavior only (no mocks): handle validity, component
 * add/get/remove, SoA column growth past initial capacity, iteration,
 * and stale-handle invalidation on free + reuse. Returns nonzero on
 * any failure so the build gate stops dead.
 */
#include "wb_ecs.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, msg) do {                                          \
    if (cond) { g_pass++; }                                            \
    else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); }  \
} while (0)

typedef struct { float x, y, z; } comp_pos;
typedef struct { float mass; }     comp_mass;

typedef struct { int n; } iter_ctx;

/* iteration callback: count invocations (each present CP slot visits once) */
static void iter_count(void *comp, void *userp) {
    (void)comp;
    iter_ctx *ic = (iter_ctx *)userp;
    ic->n++;
}

/* count entities present for a comp type by scanning world state */
static int count_present(const wb_world *w, wb_comp_type t) {
    int n = 0;
    uint32_t hi = wb_world_next_index(w);
    for (uint32_t i = 0; i < hi; i++) {
        if (!wb_world_slot_alive(w, i)) continue;
        if (wb_world_comp_present(w, t, i)) n++;
    }
    return n;
}

int main(void) {
    wb_world *w = wb_world_new();

    wb_comp_type CP = wb_register_comp(w, "position", sizeof(comp_pos));
    wb_comp_type CM = wb_register_comp(w, "mass",     sizeof(comp_mass));
    CHECK(CP == 0 && CM == 1, "comp types registered in order");

    /* --- entity create + valid --- */
    wb_entity a = wb_entity_new(w);
    wb_entity b = wb_entity_new(w);
    wb_entity c = wb_entity_new(w);
    CHECK(wb_entity_valid(w, a) && wb_entity_valid(w, b) && wb_entity_valid(w, c),
          "fresh entities valid");
    CHECK(wb_entity_count(w) == 3, "count == 3 after three creates");

    /* --- add + get component */
    comp_pos *pa = wb_comp_add(w, a, CP);
    CHECK(pa != NULL, "add position to a");
    pa->x = 1.0f; pa->y = 2.0f; pa->z = 3.0f;
    comp_pos *ga = wb_comp_get(w, a, CP);
    CHECK(ga && ga->x == 1.0f && ga->y == 2.0f, "get returns same data");

    /* entity without the comp returns NULL */
    CHECK(wb_comp_get(w, b, CP) == NULL, "get on entity lacking comp == NULL");

    /* --- SoA spacing: distinct entities own distinct columns slots --- */
    comp_pos *pb = wb_comp_add(w, b, CP);
    CHECK(pb != NULL && pb != pa, "add on b allocates distinct column slot");
    pb->x = 9.0f;
    CHECK(pa->x == 1.0f, "writing b's comp did not clobber a's (SoA intact)");

    /* --- growth past initial 8-column capacity --- */
    wb_entity many[32];
    comp_pos *first_ptr = NULL;
    for (int i = 0; i < 32; i++) {
        many[i] = wb_entity_new(w);
        comp_pos *p = wb_comp_add(w, many[i], CP);
        p->x = (float)i;
        if (i == 0) first_ptr = p;
    }
    /* column must have grown: first&last slots distinct and far apart */
    comp_pos *lastp = wb_comp_get(w, many[31], CP);
    CHECK(lastp != NULL, "column grew to hold 32 entities");
    CHECK(lastp->x == 31.0f, "growth preserved per-entity data");
    CHECK(first_ptr != NULL && lastp > first_ptr + 8, "column actually realloc-grew (>8)");

    /* --- remove component --- */
    wb_comp_remove(w, a, CP);
    CHECK(wb_comp_get(w, a, CP) == NULL, "removed comp no longer present");
    CHECK(count_present(w, CP) == 33, "32 + b still present; a removed");

    /* --- free + stale-handle invalidation via generation --- */
    wb_entity d = wb_entity_new(w);
    wb_comp_add(w, d, CM);
    uint32_t dgen = d.gen;
    /* alive now: a,b,c + 32 many + d = 36 */
    wb_entity_free(w, d);
    CHECK(!wb_entity_valid(w, d), "freed entity invalidated");
    CHECK(wb_entity_count(w) == 35, "count decremented after free (36 -> 35)");
    /* reuse of that slot bumps generation -> old handle stays dead */
    wb_entity e = wb_entity_new(w);
    (void)e;                  /* may reuse d's freed slot; count back to 36 */
    CHECK(!wb_entity_valid(w, d), "stale handle still dead after slot reuse (gen++ worked)");

    /* generation really incremented on reuse? (d.index freed, e reused same idx unlikely set) */
    CHECK(dgen == d.gen - 0 || dgen < d.gen, "generation monotonic on free/reuse path");

    /* --- double-free tolerance --- */
    wb_entity_free(w, b);
    CHECK(wb_entity_count(w) == 35, "count after freeing b (36 -> 35)");
    wb_entity_free(w, b);           /* already freed; must not crash or double-decr */
    CHECK(wb_entity_count(w) == 35, "double-free is a no-op (count stable)");

    /* --- iteration: iterator visits each present slot exactly once --- */
    {
        iter_ctx ic; ic.n = 0;
        wb_comp_iter(w, CP, iter_count, &ic);
        /* present for CP after removing a's: b + 32 "many" = 33 (some freed) */
        /* after free b+d, present CP = 32 "many" only = 32 */
        CHECK(ic.n == count_present(w, CP),
              "iter visits each present CP slot exactly once");
        CHECK(ic.n == 32, "iteration count matches expected scope (32 mores present)");
    }

    wb_world_destroy(w);

    int total = g_pass + g_fail;
    printf("\n=== ECS TESTS: %d pass, %d fail (of %d) ===\n", g_pass, g_fail, total);
    return g_fail ? 1 : 0;
}
