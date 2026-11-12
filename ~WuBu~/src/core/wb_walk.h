/*
 * wb_walk.h -- WuBu Engine procedural walk cycle.
 *
 * No skeleton, no clips, no assets. A walk cycle here is a PHASE advanced by the
 * distance a body has actually travelled, so a figure reads as walking at the
 * speed it is moving and stands still when it stops. That is the whole of
 * procedural animation: the state is one double, and it is driven by the same
 * velocity the physics step already wrote.
 *
 * The alternative -- sampling a clip on a rig -- is what wb_anim.h does, and it
 * needs a skeleton and an asset. This needs neither, which is why it is the
 * floor: it puts a leg on the screen in a build with no content pipeline.
 *
 * C11, no external deps.
 */
#ifndef WB_WALK_H
#define WB_WALK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double phase;   /* radians, wrapped into [0, 2*pi) */
    double rate;    /* radians per METRE travelled -- stride length, in disguise */
    double speed;   /* last speed fed in, m/s. Callers may read it back. */
} wb_walk;

/* `rate` is radians per metre: 2*pi per stride. A human stride is roughly 1.4 m
 * for a full cycle (two steps), so the default is 2*pi/1.4 ~= 4.49. */
#define WB_WALK_RATE_PER_METRE 4.49

void wb_walk_init(wb_walk *w, double rate);

/* Advance by `dt` seconds at `speed` m/s. Speed 0 freezes the cycle where it is
 * rather than resetting it, so a body that stops mid-stride holds its pose. */
void wb_walk_step(wb_walk *w, double dt, double speed);

/* sin(phase): -1..1. The swing term -- where a limb is in its arc. */
double wb_walk_swing(const wb_walk *w);

/* max(0, sin(phase)): 0..1. The lift term -- a foot is off the ground only while
 * it swings forward, never while it plants. */
double wb_walk_lift(const wb_walk *w);

#ifdef __cplusplus
}
#endif

#endif /* WB_WALK_H */
