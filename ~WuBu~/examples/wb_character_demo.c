/*
 * wb_character_demo.c -- slice 10 headless "playable character" demo.
 *
 * No window: this drives the same input -> controller -> physics chain a
 * windowed build would, from a scripted player. It prints the character's
 * path so the behaviour is checkable by eye (and by the test gate).
 *
 *   build/wb_character_demo.exe
 */
#include "wb_camera.h"
#include "wb_character.h"
#include "wb_input.h"
#include "wb_physics.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

int main(void) {
    wb_physics *p = wb_physics_new();
    wb_physics_add_ground(p, "ground");

    /* A few obstacles to walk around and onto. */
    wb_physics_add_body(p, WB_PHYS_BOX, "block_a", (double[3]){3.0,  0.5, 0.25},
                        (double[3]){0.5, 0.5, 0.25}, 0.0);
    wb_physics_add_body(p, WB_PHYS_BOX, "block_b", (double[3]){6.0, -1.0, 0.40},
                        (double[3]){0.5, 0.5, 0.40}, 0.0);
    wb_physics_add_body(p, WB_PHYS_BOX, "wall",    (double[3]){9.0,  1.0, 0.75},
                        (double[3]){0.2, 1.5, 0.75}, 0.0);

    int hero = wb_physics_add_character(p, "hero", (double[3]){0, 0, 2.0}, 0.30, 1.0, 80.0);
    if (wb_physics_build(p) != 0) {
        printf("build failed: %s\n", wb_physics_error(p));
        return 1;
    }

    wb_camera cam; wb_camera_init(&cam);
    wb_character c; wb_character_init(&c);
    wb_input in;    wb_input_init(&in);

    /* Scripted player: 2s forward, 1s forward-left, 1s jump, 2s forward. */
    struct { double fwd, back, left, right, jump, sprint; int frames; } script[] = {
        { 1, 0, 0, 0, 0, 0, 120 },
        { 1, 0, 1, 0, 0, 0,  60 },
        { 0, 0, 0, 0, 1, 0,  12 },
        { 0, 0, 0, 0, 0, 0,  48 },
        { 1, 0, 0, 1, 0, 1, 120 },
    };
    int nscript = (int)(sizeof script / sizeof script[0]);

    printf("character demo -- %d bodies, scripted player\n",
           wb_physics_body_count(p));
    printf("  t(s)   x      y      z     yaw    spd    grounded\n");

    /* settle */
    for (int i = 0; i < 120; i++) wb_physics_step(p, 1.0/60.0);

    double t = 0.0;
    for (int s = 0; s < nscript; s++) {
        wb_actions a;
        memset(&a, 0, sizeof a);
        a.fwd = script[s].fwd; a.back = script[s].back;
        a.left = script[s].left; a.right = script[s].right;
        a.jump = script[s].jump; a.sprint = script[s].sprint;

        for (int i = 0; i < script[s].frames; i++) {
            double pos[3]; wb_physics_get_pos(p, hero, pos);
            wb_camera_update(&cam, pos, 0.2);
            wb_camera_orbit(&cam, in.mouse_dx, in.mouse_dy, 0.01);

            wb_character_apply(&c, p, hero, &a, &cam, 1.0/60.0);
            wb_physics_step(p, 1.0/60.0);
            wb_input_end_frame(&in);
            t += 1.0/60.0;

            if (i % 30 == 0) {
                double v[3]; wb_physics_get_vel(p, hero, v);
                double spd = sqrt(v[0]*v[0] + v[1]*v[1]);
                printf("  %4.1f  %6.2f %6.2f %6.2f  %5.2f  %5.2f   %s\n",
                       t, pos[0], pos[1], pos[2], c.yaw, spd,
                       wb_physics_grounded(p, hero) ? "yes" : "no");
            }
        }
    }

    double end[3]; wb_physics_get_pos(p, hero, end);
    printf("\nfinal position: (%.2f, %.2f, %.2f)  facing %.2f rad\n",
           end[0], end[1], end[2], c.yaw);
    printf("camera eye:     (%.2f, %.2f, %.2f)\n",
           cam.eye[0], cam.eye[1], cam.eye[2]);

    wb_physics_free(p);
    return 0;
}
