/*
 * wb_camera.h -- WuBu Engine follow/orbit camera (slice 10).
 *
 * z-up world (MuJoCo convention). The camera orbits a target (usually the
 * player character) at a yaw/pitch/distance; `wb_camera_eye` returns the world
 * position and `wb_camera_forward` the horizontal look direction, which is
 * what movement input is expressed in.
 */
#ifndef WB_CAMERA_H
#define WB_CAMERA_H

typedef struct {
    double target[3];   /* what we look at */
    double yaw;         /* radians, 0 looks along +x */
    double pitch;       /* radians, + is above */
    double dist;        /* metres */
    double eye[3];      /* computed */
} wb_camera;

void wb_camera_init(wb_camera *cam);

/* Mouse/touch orbit. `dx`,`dy` are pixels or normalised units. */
void wb_camera_orbit(wb_camera *cam, double dx, double dy, double sensitivity);

/* Move the target (typically to the character position) and recompute eye. */
void wb_camera_update(wb_camera *cam, const double target_pos[3], double smoothing);

void wb_camera_eye(const wb_camera *cam, double out[3]);
void wb_camera_forward(const wb_camera *cam, double out[3]);   /* unit, horizontal */
void wb_camera_right(const wb_camera *cam, double out[3]);     /* unit, horizontal */

/* Clamp helper so pitch cannot flip over the poles. */
void wb_camera_clamp(wb_camera *cam, double min_pitch, double max_pitch,
                     double min_dist, double max_dist);

#endif /* WB_CAMERA_H */
