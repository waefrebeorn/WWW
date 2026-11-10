/*
 * wb_snap_test.c -- LP-03 capstone gate: world digest + rollback restore.
 *
 * Proves the snapshot digest is the checksum a replay/rollback can trust and
 * that restore() actually rolls back state:
 *   - capture is pure; an independently rebuilt, identically stepped world
 *     reproduces the same digest bit-for-bit
 *   - perturbation, divergence and entity removal all change the digest
 *   - the rollback circle: capture -> step -> restore -> capture reproduces
 *     the original digest, and every record is written back
 */
#include "wb_snap.h"
#include "wb_det.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

typedef struct { float x, y, z; } Pos;
typedef struct { float x, y, z; } Vel;

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

/* deterministic integration tick: pos += vel for every live entity */
static void step_all(wb_world *w, wb_comp_type pos, wb_comp_type vel) {
    uint32_t ni = wb_world_next_index(w);
    for (uint32_t i = 0; i < ni; i++) {
        if (!wb_world_slot_alive(w, i)) continue;
        Pos *p = (Pos *)((unsigned char *)wb_world_comp_data(w, pos) + (size_t)i * sizeof(Pos));
        Vel *v = (Vel *)((unsigned char *)wb_world_comp_data(w, vel) + (size_t)i * sizeof(Vel));
        p->x += v->x; p->y += v->y; p->z += v->z;
    }
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
    CHECK(wb_snap_digest(a1) != 0, "digest non-zero for a populated world");
    CHECK(wb_snap_bytes(a1) > 0, "snapshot holds packed bytes");
    CHECK(wb_snap_equal(a1, a2), "capture is pure: same world, same digest");
    CHECK(wb_snap_equal(a1, b), "independently rebuilt identical sim reproduces the digest");

    /* one-entity perturbation changes the digest */
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

    /* ---- rollback circle: capture -> step -> restore -> capture ---------- */
    build(7, &wa, ta);
    wb_snap *r0 = wb_snap_capture(wa, ta, 2);
    step_all(wa, ta[0], ta[1]);
    step_all(wa, ta[0], ta[1]);
    wb_snap *r1 = wb_snap_capture(wa, ta, 2);
    CHECK(!wb_snap_equal(r0, r1), "deterministic stepping changes the snapshot");

    int n = wb_snap_restore(wa, ta, 2, r0);
    CHECK(n == 100, "restore wrote back every captured entity");
    wb_snap *r2 = wb_snap_capture(wa, ta, 2);
    CHECK(wb_snap_equal(r0, r2), "rollback reproduces the original digest");

    /* restoring over an entity that was removed is a safe no-op for it */
    step_all(wa, ta[0], ta[1]);
    wb_entity rmv = { 5, wb_world_slot_gen(wa, 5) };
    wb_entity_free(wa, rmv);
    int n2 = wb_snap_restore(wa, ta, 2, r0);
    CHECK(n2 == 99, "restore skips the removed entity, restores the rest");

    wb_snap_free(a1); wb_snap_free(a2); wb_snap_free(b); wb_snap_free(a3);
    wb_snap_free(other); wb_snap_free(fewer);
    wb_snap_free(r0); wb_snap_free(r1); wb_snap_free(r2);
    wb_world_destroy(wa); wb_world_destroy(wb_);

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
