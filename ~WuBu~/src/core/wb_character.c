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

void wb_character_apply_body(wb_character *c, const wb_character_body *b,
                             const wb_actions *a, const wb_camera *cam,
                             double dt) {
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
    double v[3]; b->get_vel(b->ctx, v);
    double dvx = des_x - v[0], dvy = des_y - v[1];
    double dv = sqrt(dvx * dvx + dvy * dvy);
    double maxdv = c->accel * dt;
    if (dv > maxdv && dv > 1e-9) { dvx = dvx / dv * maxdv; dvy = dvy / dv * maxdv; }

    double nv[3] = { v[0] + dvx, v[1] + dvy, v[2] };
    b->set_vel(b->ctx, nv);

    /* Face the direction of travel. */
    if (len > 1e-6 && b->set_yaw) {
        double target = atan2(des_y, des_x);
        double diff = target - c->yaw;
        while (diff >  WB_PI) diff -= 2.0 * WB_PI;
        while (diff < -WB_PI) diff += 2.0 * WB_PI;
        double maxturn = c->turn_rate * dt;
        if (diff >  maxturn) diff =  maxturn;
        if (diff < -maxturn) diff = -maxturn;
        c->yaw += diff;
        b->set_yaw(b->ctx, c->yaw);
    }

    /* Jump only from the ground. A backend with no ground notion skips this.
     * The write is a direct velocity set: an impulse is not a desired velocity
     * and must not go through the character-move path. */
    if (a->jump && b->grounded && b->grounded(b->ctx)) {
        double jv[3]; b->get_vel(b->ctx, jv);
        double up[3] = { jv[0], jv[1], c->jump_speed };
        if (b->set_vel_direct) b->set_vel_direct(b->ctx, up);
        else                   b->set_vel(b->ctx, up);
    }
}

/* Sprint is deliberately excluded: it is a modifier, not a direction. Holding
 * shift alone must not seize the body from the mode's script. */
int wb_actions_any(const wb_actions *a) {
    return a && (a->fwd || a->back || a->left || a->right || a->jump);
}
