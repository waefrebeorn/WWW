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

/* The backend-neutral form of the controller.
 *
 * The controller needs exactly four things from a body -- read its velocity,
 * write its velocity, ask whether it is on the ground, and set its facing. It
 * does not need a physics engine, so a host can supply those over whatever it
 * already owns: an ECS component array, a networking buffer, a replay log. This
 * is what keeps the movement math in one place instead of one place per backend.
 *
 * `set_vel` receives the desired horizontal velocity; `set_yaw` the facing. A
 * callback may be NULL when the backend has no such notion (an ECS world has no
 * ground plane), in which case that feature is skipped rather than faked. */
typedef struct wb_character_body {
    void   *ctx;
    void  (*get_vel)(void *ctx, double v[3]);
    void  (*set_vel)(void *ctx, const double v[3]);
    /* The same write, bypassing whatever the backend's character-move path does
     * to a desired velocity. Jumping is an impulse, not a desired velocity, so
     * it must not be clamped or smoothed -- this is why the callback exists
     * separately. May be NULL, in which case set_vel is used. */
    void  (*set_vel_direct)(void *ctx, const double v[3]);
    int   (*grounded)(void *ctx);       /* may be NULL */
    void  (*set_yaw)(void *ctx, double yaw);  /* may be NULL */
} wb_character_body;

void wb_character_apply_body(wb_character *c, const wb_character_body *b,
                             const wb_actions *a, const wb_camera *cam,
                             double dt);

/* True when any movement action is held. A host uses this to decide whether the
 * controller or the mode's own script is driving the body this frame: with no
 * actions the controller must leave the velocity alone, or it would silently
 * brake whatever the mode asked for. */
int wb_actions_any(const wb_actions *a);

#endif /* WB_CHARACTER_H */
