/*
 * wb_snap_test.c -- LP-03 stone 3 gate: the per-tick world digest.
 *
 * Proves the snapshot digest is the checksum a replay/rollback can trust:
 *   - capture is pure (twice on the same world, same digest)
 *   - an independently rebuilt, identically stepped world reproduces the same
 *     digest (two runs that should agree, agree bit-for-bit)
 *   - a one-entity perturbation changes the digest (they catch divergence)
 *   - a differently seeded sim diverges (they catch go-live nondeterminism)
 */
#include "wb_snap.h"
#include "wb_det.h"

#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

typedef struct { float x, y, z; } Pos;
typedef struct { float x, y, z; } Vel;

/* Build a world with 100 entities whose pos/vel derive from a seeded stream,
 * so an identically seeded rebuild is byte-identical. */
static void build(uint64_t seed, wb_world **out, wb_comp_type types[2]) {
    wb_world *w = wb_world_new();
    types[0] = wb_register_comp(w, "pos", sizeof(Pos));
    types[1] = wb_register_comp(w, "vel", sizeof(Vel));
    wb_det_rng r;
    wb_det_rng_seed(&r, seed);
    for (int n = 0; n < 100; n++) {
        wb_entity e = wb_entity_new(w);
        Pos *p = (Pos *)wb_comp_add(w, e, types[0]);
        Vel *v = (Vel *)wb_comp_add(w, e, types[1]);
        p->x = (float)(wb_det_rng_f01(&r) * 10.0);
        p->y = (float)(wb_det_rng_f01(&r) * 10.0);
        p->z = (float)(wb_det_rng_f01(&r) * 10.0);
        v->x = (float)wb_det_rng_f01(&r);
        v->y = (float)wb_det_rng_f01(&r);
        v->z = (float)wb_det_rng_f01(&r);
    }
    *out = w;
}

int main(void) {
    wb_comp_type ta[2], tb[2];
    wb_world *wa, *wb_;

    build(7, &wa, ta);
    build(7, &wb_, tb);

    wb_snap *a1 = wb_snap_capture(wa, ta, 2);
    wb_snap *a2 = wb_snap_capture(wa, ta, 2);
    wb_snap *b  = wb_snap_capture(wb_, tb, 2);
    CHECK(a1 && a2 && b, "snapshots captured");
    CHECK(wb_snap_digest(a1) != 0, "digest is non-zero for a populated world");
    CHECK(wb_snap_equal(a1, a2), "capture is pure: same world, same digest");
    CHECK(wb_snap_equal(a1, b), "independently rebuilt identical sim reproduces the digest");

    /* one-entity perturbation must change the digest */
    wb_entity e0 = { 0, wb_world_slot_gen(wa, 0) };
    Pos *p0 = (Pos *)wb_comp_get(wa, e0, ta[0]);
    if (p0) p0->x += 0.5f;
    wb_snap *a3 = wb_snap_capture(wa, ta, 2);
    CHECK(!wb_snap_equal(a1, a3), "a single entity moved changes the digest");

    /* a differently seeded sim diverges */
    build(99, &wa, ta);
    wb_snap *other = wb_snap_capture(wa, ta, 2);
    CHECK(!wb_snap_equal(a1, other), "a different seed yields a different world digest");

    /* removing an entity changes the snapshot */
    build(7, &wa, ta);
    wb_entity e1 = { 3, wb_world_slot_gen(wa, 3) };
    wb_entity_free(wa, e1);
    wb_snap *fewer = wb_snap_capture(wa, ta, 2);
    CHECK(fewer != NULL && !wb_snap_equal(fewer, a1), "entity removal changes the digest");

    wb_snap_free(a1); wb_snap_free(a2); wb_snap_free(b); wb_snap_free(a3);
    wb_snap_free(other); wb_snap_free(fewer);
    wb_world_destroy(wa); wb_world_destroy(wb_);

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
