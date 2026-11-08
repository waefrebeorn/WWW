/*
 * wb_physics_test.c -- slice 9 gate: MuJoCo-backed physics.
 *
 * Proves the physics pillar end to end: a box falls under gravity and lands on
 * a plane at the geometrically correct height; a capsule character falls,
 * lands, and walks when driven; and the ECS binding keeps an entity's position
 * in lockstep with its physics body.
 */
#include "wb_physics.h"
#include "wb_ecs.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static int nearly(double a, double b, double tol) { return fabs(a - b) <= tol; }

/* Build a world with a ground plane and one dynamic body of the given kind. */
static wb_physics *mk_world(int kind, const char *name, double z,
                            const double size[3], double mass) {
    wb_physics *p = wb_physics_new();
    CHECK(p != NULL, "physics world created");
    wb_physics_add_ground(p, "ground");
    int id = wb_physics_add_body(p, kind, name, (double[3]){0, 0, z}, size, mass);
    CHECK(id >= 1, "body added with a valid id");
    CHECK(wb_physics_build(p) == 0, "world built");
    return p;
}

int main(void) {
    /* ---- 1: a box falls and lands at its half-height ------------------- */
    {
        double size[3] = {0.10, 0.10, 0.10};
        wb_physics *p = mk_world(WB_PHYS_BOX, "box", 1.0, size, 1.0);
        CHECK(wb_physics_body_count(p) == 2, "world body + box = 2 bodies");
        CHECK(wb_physics_find(p, "box") == 1, "box found by name at id 1");

        double start[3];
        wb_physics_get_pos(p, 1, start);
        CHECK(nearly(start[2], 1.0, 1e-6), "box starts at z=1.0");

        for (int i = 0; i < 500; i++) wb_physics_step(p, 0.002);   /* 1 second */

        double rest[3];
        wb_physics_get_pos(p, 1, rest);
        /* Free fall for 1s would be 1 - 4.9 = -3.9; a resting box must be
         * near its half-extent (0.10), i.e. it actually collided. */
        CHECK(rest[2] > 0.05 && rest[2] < 0.16,
              "box fell and rests on the plane near z=0.10");
        CHECK(nearly(rest[0], 0.0, 1e-3) && nearly(rest[1], 0.0, 1e-3),
              "box did not drift sideways while falling");

        /* It should be (nearly) at rest now. */
        double v[3]; wb_physics_get_vel(p, 1, v);
        CHECK(nearly(v[2], 0.0, 1e-2), "box vertical velocity ~0 at rest");
        wb_physics_free(p);
    }

    /* ---- 2: a sphere lands at its radius ------------------------------- */
    {
        double size[3] = {0.25, 0, 0};
        wb_physics *p = mk_world(WB_PHYS_SPHERE, "ball", 1.0, size, 2.0);
        for (int i = 0; i < 500; i++) wb_physics_step(p, 0.002);
        double rest[3]; wb_physics_get_pos(p, 1, rest);
        CHECK(rest[2] > 0.20 && rest[2] < 0.30,
              "sphere rests on the plane near z=radius=0.25");
        wb_physics_free(p);
    }

    /* ---- 3: a capsule character falls, lands, and walks --------------- */
    {
        double radius = 0.30, halfh = 0.50;
        wb_physics *p = wb_physics_new();
        wb_physics_add_ground(p, "ground");
        int c = wb_physics_add_character(p, "hero", (double[3]){0, 0, 2.0},
                                         radius, halfh * 2.0, 80.0);
        CHECK(c == 1, "character body id is 1");
        CHECK(wb_physics_build(p) == 0, "character world built");

        for (int i = 0; i < 400; i++) wb_physics_step(p, 0.002);   /* 0.8 s */
        double rest[3]; wb_physics_get_pos(p, 1, rest);
        /* Resting center = half-height + radius = 0.8 above the plane. */
        CHECK(rest[2] > 0.6 && rest[2] < 1.0,
              "character landed and stands at a plausible height");

        /* Drive it forward for 1 second; it should translate in +x. */
        double before[3]; wb_physics_get_pos(p, 1, before);
        for (int i = 0; i < 500; i++) {
            wb_physics_character_move(p, 1, (double[3]){2.0, 0.0, 0.0});
            wb_physics_step(p, 0.002);
        }
        double after[3]; wb_physics_get_pos(p, 1, after);
        CHECK(after[0] - before[0] > 0.5,
              "character translates horizontally when driven");
        CHECK(after[2] > 0.4, "character stays on the ground while walking");
        wb_physics_free(p);
    }

    /* ---- 4: ECS binding keeps an entity in lockstep with its body ----- */
    {
        wb_physics *p = wb_physics_new();
        wb_physics_add_ground(p, "ground");
        int box = wb_physics_add_body(p, WB_PHYS_BOX, "crate",
                                      (double[3]){0, 0, 0.5},
                                      (double[3]){0.2, 0.2, 0.2}, 1.0);
        CHECK(wb_physics_build(p) == 0, "bound world built");

        wb_world w; wb_world_init(&w);
        wb_register_comp(&w, "position", sizeof(float) * 3);
        wb_register_comp(&w, "velocity", sizeof(float) * 3);
        wb_entity e = wb_entity_new(&w);
        float *pos = (float *)wb_comp_add(&w, e, 0);
        pos[0] = pos[1] = pos[2] = 0.0f;

        wb_physics_bind(p, box, e);
        CHECK(wb_physics_bind_count(p) == 1, "one binding registered");

        for (int i = 0; i < 300; i++) {
            wb_physics_sync_in(p, &w);
            wb_physics_step(p, 0.002);
            wb_physics_sync_out(p, &w);
        }
        double bp[3]; wb_physics_get_pos(p, box, bp);
        CHECK(nearly(pos[0], bp[0], 1e-4) && nearly(pos[1], bp[1], 1e-4) &&
              nearly(pos[2], bp[2], 1e-4),
              "ECS position matches the physics body after stepping");
        CHECK(pos[2] > 0.15 && pos[2] < 0.25,
              "bound crate settled on the plane (z ~ 0.20)");

        wb_world_free(&w);
        wb_physics_free(p);
    }

    /* ---- 5: a body cannot be added after build ------------------------ */
    {
        double size[3] = {0.1, 0.1, 0.1};
        wb_physics *p = mk_world(WB_PHYS_BOX, "b", 1.0, size, 1.0);
        int rc = wb_physics_add_body(p, WB_PHYS_BOX, "late",
                                     (double[3]){0, 0, 1}, size, 1.0);
        CHECK(rc == -1, "adding a body after build is rejected");
        CHECK(wb_physics_error(p) != NULL, "an error message is recorded");
        wb_physics_free(p);
    }

    printf("\n=== PHYSICS TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
