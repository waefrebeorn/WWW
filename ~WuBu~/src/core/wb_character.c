/*
 * wb_character.c -- WuBu Engine character controller (slice 10).
 * See wb_character.h.
 */
#include "wb_character.h"

#include <math.h>

#ifndef WB_PI
#define WB_PI 3.14159265358979323846
#endif

void wb_character_init(wb_character *c) {
    c->speed      = 3.0;
    c->sprint_mul = 1.8;
    c->accel      = 20.0;
    c->turn_rate  = 10.0;
    c->jump_speed = 5.0;
    c->yaw        = 0.0;
}

void wb_character_apply(wb_character *c, wb_physics *p, int body,
                        const wb_actions *a, const wb_camera *cam, double dt) {
    double f[3], r[3];
    wb_camera_forward(cam, f);
    wb_camera_right(cam, r);

    /* Camera-relative movement intent. */
    double ix = (double)(a->right ? 1 : 0) - (double)(a->left ? 1 : 0);
    double iz = (double)(a->fwd   ? 1 : 0) - (double)(a->back ? 1 : 0);

    double dx = f[0] * iz + r[0] * ix;
    double dy = f[1] * iz + r[1] * ix;
    double len = sqrt(dx * dx + dy * dy);

    double speed = c->speed * ((a->sprint && iz > 0) ? c->sprint_mul : 1.0);
    double des_x = 0.0, des_y = 0.0;
    if (len > 1e-6) { des_x = dx / len * speed; des_y = dy / len * speed; }

    /* Accelerate toward the desired velocity (never teleport). */
    double v[3]; wb_physics_get_vel(p, body, v);
    double dvx = des_x - v[0], dvy = des_y - v[1];
    double dv = sqrt(dvx * dvx + dvy * dvy);
    double maxdv = c->accel * dt;
    if (dv > maxdv && dv > 1e-9) { dvx = dvx / dv * maxdv; dvy = dvy / dv * maxdv; }

    double nv[3] = { v[0] + dvx, v[1] + dvy, v[2] };
    wb_physics_character_move(p, body, nv);

    /* Face the direction of travel. */
    if (len > 1e-6) {
        double target = atan2(des_y, des_x);
        double diff = target - c->yaw;
        while (diff >  WB_PI) diff -= 2.0 * WB_PI;
        while (diff < -WB_PI) diff += 2.0 * WB_PI;
        double maxturn = c->turn_rate * dt;
        if (diff >  maxturn) diff =  maxturn;
        if (diff < -maxturn) diff = -maxturn;
        c->yaw += diff;
        wb_physics_set_yaw(p, body, c->yaw);
    }

    /* Jump only from the ground. */
    if (a->jump && wb_physics_grounded(p, body)) {
        double jv[3]; wb_physics_get_vel(p, body, jv);
        double up[3] = { jv[0], jv[1], c->jump_speed };
        wb_physics_set_vel(p, body, up);
    }
}
