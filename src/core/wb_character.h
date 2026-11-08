/*
 * wb_character.h -- WuBu Engine character controller (slice 10).
 *
 * Turns high-level actions (move/jump/sprint) plus a camera into a desired
 * horizontal velocity for a physics character, and turns the character to face
 * where it is going. The physics world stays authoritative -- this only writes
 * velocities and facing, never positions.
 */
#ifndef WB_CHARACTER_H
#define WB_CHARACTER_H

#include "wb_camera.h"
#include "wb_physics.h"

/* High-level actions. The platform layer maps keys/buttons onto these. */
typedef struct {
    int fwd, back, left, right;
    int jump, sprint;
} wb_actions;

typedef struct {
    double speed;        /* walk speed, m/s */
    double sprint_mul;   /* multiplier while sprinting */
    double accel;        /* m/s^2 toward the desired velocity */
    double turn_rate;    /* rad/s the character can turn */
    double jump_speed;   /* m/s upward impulse */
    double yaw;          /* current facing, radians */
} wb_character;

void wb_character_init(wb_character *c);

/* Apply one frame of actions. `cam` supplies the movement basis (camera
 * relative controls: "forward" is where the camera looks). */
void wb_character_apply(wb_character *c, wb_physics *p, int body,
                        const wb_actions *a, const wb_camera *cam, double dt);

#endif /* WB_CHARACTER_H */
