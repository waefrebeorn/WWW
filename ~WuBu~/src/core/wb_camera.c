/*
 * wb_camera.c -- WuBu Engine follow/orbit camera (slice 10). See wb_camera.h.
 */
#include "wb_camera.h"

#include <math.h>
#include <string.h>

#ifndef WB_PI
#define WB_PI 3.14159265358979323846
#endif

void wb_camera_init(wb_camera *cam) {
    memset(cam, 0, sizeof *cam);
    cam->target[0] = cam->target[1] = cam->target[2] = 0.0;
    cam->yaw   = WB_PI * 0.25;
    cam->pitch = 0.35;
    cam->dist  = 6.0;
    cam->eye[0] = cam->eye[1] = cam->eye[2] = 0.0;
}

void wb_camera_orbit(wb_camera *cam, double dx, double dy, double sensitivity) {
    cam->yaw   -= (float)(dx * sensitivity);
    cam->pitch += (float)(dy * sensitivity);
    /* keep yaw bounded so trig stays well behaved over long sessions */
    if (cam->yaw >  WB_PI) cam->yaw -= 2.0 * WB_PI;
    if (cam->yaw < -WB_PI) cam->yaw += 2.0 * WB_PI;
    wb_camera_clamp(cam, -1.4, 1.4, 1.5, 60.0);
}

void wb_camera_update(wb_camera *cam, const double target_pos[3], double smoothing) {
    // exponential smoothing toward the target; smoothing in [0,1]
    double t = smoothing < 0.0 ? 0.0 : (smoothing > 1.0 ? 1.0 : smoothing);
    for (int i = 0; i < 3; i++)
        cam->target[i] += (target_pos[i] - cam->target[i]) * t;

    double cp = cos(cam->pitch), sp = sin(cam->pitch);
    double cy = cos(cam->yaw),   sy = sin(cam->yaw);
    /* horizontal forward (where the camera looks) */
    double fx = cy * cp, fy = sy * cp, fz = sp;
    cam->eye[0] = cam->target[0] - fx * cam->dist;
    cam->eye[1] = cam->target[1] - fy * cam->dist;
    cam->eye[2] = cam->target[2] - fz * cam->dist;
}

void wb_camera_eye(const wb_camera *cam, double out[3]) {
    out[0] = cam->eye[0]; out[1] = cam->eye[1]; out[2] = cam->eye[2];
}

void wb_camera_forward(const wb_camera *cam, double out[3]) {
    double cp = cos(cam->pitch), sp = sin(cam->pitch);
    double fx = cos(cam->yaw) * cp, fy = sin(cam->yaw) * cp, fz = sp;
    /* project to horizontal so movement never flies up/down with pitch */
    double len = sqrt(fx * fx + fy * fy);
    if (len < 1e-9) { out[0] = 1.0; out[1] = 0.0; out[2] = 0.0; return; }
    out[0] = fx / len; out[1] = fy / len; out[2] = 0.0;
    (void)fz;
}

void wb_camera_right(const wb_camera *cam, double out[3]) {
    double f[3]; wb_camera_forward(cam, f);
    /* right = forward x up (z-up world) -> (fy, -fx, 0) */
    out[0] =  f[1];
    out[1] = -f[0];
    out[2] =  0.0;
}

void wb_camera_clamp(wb_camera *cam, double min_pitch, double max_pitch,
                     double min_dist, double max_dist) {
    if (cam->pitch < min_pitch) cam->pitch = min_pitch;
    if (cam->pitch > max_pitch) cam->pitch = max_pitch;
    if (cam->dist  < min_dist)  cam->dist  = min_dist;
    if (cam->dist  > max_dist)  cam->dist  = max_dist;
}
