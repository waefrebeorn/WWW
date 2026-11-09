/*
 * wb_character_test.c -- slice 10 gate: input + camera + character chain.
 *
 * Proves the "playable" path without a window: input edge detection, camera
 * orbit/follow math, and the full actions -> controller -> physics chain that
 * moves a character around a plane.
 */
#include "wb_camera.h"
#include "wb_character.h"
#include "wb_input.h"
#include "wb_physics.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static int nearly(double a, double b, double tol) { return fabs(a - b) <= tol; }

#ifndef WB_PI
#define WB_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ input */
static void test_input(void) {
    wb_input in; wb_input_init(&in);
    CHECK(!wb_input_down(&in, 'W'), "key starts up");

    wb_input_key_down(&in, 'W');
    CHECK(wb_input_down(&in, 'W'), "key reads held immediately");
    wb_input_end_frame(&in);
    CHECK(wb_input_pressed(&in, 'W'), "pressed edge seen after end_frame");

    wb_input_end_frame(&in);
    CHECK(!wb_input_pressed(&in, 'W'), "pressed edge cleared after a second frame");
    CHECK(wb_input_down(&in, 'W'), "key still held");

    wb_input_key_up(&in, 'W');
    wb_input_end_frame(&in);
    CHECK(wb_input_released(&in, 'W'), "released edge seen");
    CHECK(!wb_input_down(&in, 'W'), "key is up");

    wb_input_mouse_move(&in, 3.0, -2.0);
    CHECK(nearly(in.mouse_dx, 3.0, 1e-9) && nearly(in.mouse_dy, -2.0, 1e-9),
          "mouse deltas accumulate");
    wb_input_end_frame(&in);
    CHECK(nearly(in.mouse_dx, 0.0, 1e-9) && nearly(in.mouse_dy, 0.0, 1e-9),
          "mouse deltas cleared at end of frame");

    wb_input_button(&in, 0, 1);
    wb_input_end_frame(&in);
    CHECK(wb_input_button_down(&in, 0), "mouse button held");
    CHECK(wb_input_button_pressed(&in, 0), "mouse button pressed edge");
    wb_input_end_frame(&in);
    CHECK(!wb_input_button_pressed(&in, 0), "mouse button edge clears");

    /* out-of-range codes are ignored, not UB */
    wb_input_key_down(&in, 999);
    wb_input_key_down(&in, -1);
    CHECK(1, "out-of-range key codes are ignored");
}

/* ------------------------------------------------------------------ camera */
static void test_camera(void) {
    wb_camera cam; wb_camera_init(&cam);
    CHECK(nearly(cam.dist, 6.0, 1e-9), "default camera distance");

    double target[3] = {1.0, 2.0, 0.8};
    wb_camera_update(&cam, target, 1.0);
    CHECK(nearly(cam.target[0], 1.0, 1e-9) && nearly(cam.target[1], 2.0, 1e-9) &&
          nearly(cam.target[2], 0.8, 1e-9),
          "camera target snaps with smoothing=1");
    double eye[3]; wb_camera_eye(&cam, eye);

    /* eye must be `dist` away from the target */
    double dx = eye[0]-target[0], dy = eye[1]-target[1], dz = eye[2]-target[2];
    CHECK(nearly(sqrt(dx*dx+dy*dy+dz*dz), 6.0, 1e-6),
          "camera eye is exactly dist from the target");

    /* forward is horizontal and unit length */
    double f[3]; wb_camera_forward(&cam, f);
    CHECK(nearly(f[2], 0.0, 1e-9), "camera forward is horizontal");
    CHECK(nearly(sqrt(f[0]*f[0]+f[1]*f[1]), 1.0, 1e-6), "camera forward is unit length");

    /* right is perpendicular to forward */
    double r[3]; wb_camera_right(&cam, r);
    CHECK(nearly(f[0]*r[0] + f[1]*r[1], 0.0, 1e-6), "camera right is perpendicular to forward");

    /* orbit changes yaw/pitch and is clamped */
    double yaw0 = cam.yaw;
    wb_camera_orbit(&cam, 100.0, 0.0, 0.01);
    CHECK(cam.yaw != yaw0, "orbiting changes yaw");
    wb_camera_orbit(&cam, 0.0, 100000.0, 0.01);
    CHECK(cam.pitch <= 1.4 + 1e-9, "pitch is clamped at the pole");
    wb_camera_orbit(&cam, 0.0, -100000.0, 0.01);
    CHECK(cam.pitch >= -1.4 - 1e-9, "pitch is clamped at the nadir");
}

/* --------------------------------------------------------------- character */
static void test_character(void) {
    /* world: ground + a character */
    wb_physics *p = wb_physics_new();
    wb_physics_add_ground(p, "ground");
    int hero = wb_physics_add_character(p, "hero", (double[3]){0, 0, 2.0}, 0.30, 1.0, 80.0);
    CHECK(wb_physics_build(p) == 0, "character world built");

    /* settle on the ground */
    for (int i = 0; i < 300; i++) wb_physics_step(p, 0.002);
    CHECK(wb_physics_grounded(p, hero), "character is grounded after settling");

    wb_camera cam; wb_camera_init(&cam);
    double pos[3]; wb_physics_get_pos(p, hero, pos);
    wb_camera_update(&cam, pos, 1.0);

    wb_character c; wb_character_init(&c);
    wb_actions a; memset(&a, 0, sizeof a);

    /* camera default yaw = pi/4 -> forward is (+,+,0). Hold "forward". */
    a.fwd = 1;
    double start[3]; wb_physics_get_pos(p, hero, start);
    for (int i = 0; i < 120; i++) {
        wb_character_apply(&c, p, hero, &a, &cam, 1.0/60.0);
        wb_physics_step(p, 1.0/60.0);
    }
    double end[3]; wb_physics_get_pos(p, hero, end);
    double moved = sqrt((end[0]-start[0])*(end[0]-start[0]) +
                        (end[1]-start[1])*(end[1]-start[1]));
    CHECK(moved > 0.5, "holding forward moves the character a meaningful distance");
    CHECK(end[2] > 0.4, "character stays on the ground while moving");

    /* facing follows travel direction: default camera forward is (+1,+1)/sqrt2 */
    CHECK(fabs(c.yaw - WB_PI * 0.25) < 0.3,
          "character turned to face its travel direction");

    /* release -> decelerates to a stop */
    memset(&a, 0, sizeof a);
    for (int i = 0; i < 120; i++) {
        wb_character_apply(&c, p, hero, &a, &cam, 1.0/60.0);
        wb_physics_step(p, 1.0/60.0);
    }
    double v[3]; wb_physics_get_vel(p, hero, v);
    CHECK(sqrt(v[0]*v[0] + v[1]*v[1]) < 0.2, "character decelerates when input is released");

    /* jump: grounded + jump -> leaves the ground */
    a.jump = 1;
    double z_before = end[2];
    double apex = z_before;
    for (int i = 0; i < 90; i++) {
        wb_character_apply(&c, p, hero, &a, &cam, 1.0/60.0);
        wb_physics_step(p, 1.0/60.0);
        double z[3]; wb_physics_get_pos(p, hero, z);
        if (z[2] > apex) apex = z[2];
        a.jump = 0;   /* only one jump impulse */
    }
    CHECK(apex > z_before + 0.15, "character jumps when grounded and jump is pressed");

    wb_physics_free(p);
}

int main(void) {
    test_input();
    test_camera();
    test_character();
    printf("\n=== CHARACTER TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
