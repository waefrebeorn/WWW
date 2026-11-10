/*
 * wb_snapshot.h -- snapshot interpolation (LP-29).
 *
 * A client renders slightly in the past and interpolates between the two
 * snapshots that bracket the render time. Positions lerp; orientations nlerp
 * along the shortest arc, which is exact enough at snapshot spacing and far
 * cheaper than a slerp.
 *
 * C11, no deps.
 */
#ifndef WB_SNAPSHOT_H
#define WB_SNAPSHOT_H

#include <stdint.h>

typedef struct {
    float px, py, pz;
    float qx, qy, qz, qw;   /* unit quaternion */
} wb_snapshot_state;

/* Interpolate from `a` to `b` at t in [0,1] into `out`. */
void wb_snapshot_interp(const wb_snapshot_state *a, const wb_snapshot_state *b,
                        float t, wb_snapshot_state *out);

/* The interpolated quaternion's magnitude -- should stay ~1. */
float wb_snapshot_interp_error(const wb_snapshot_state *a, const wb_snapshot_state *b,
                               float t, const wb_snapshot_state *out);

/* Re-normalize a quaternion in place. */
void wb_snapshot_normalize(wb_snapshot_state *s);

#endif /* WB_SNAPSHOT_H */
