/*
 * wb_character_mj.c -- MuJoCo adapter for the character controller.
 *
 * The controller itself (wb_character.c) is backend-neutral: it asks a body for
 * four things and never mentions a physics engine. This file is the adapter that
 * supplies those four over MuJoCo, and it is the ONLY place the controller and
 * MuJoCo meet -- which is why the host, the tests and the engine binary can all
 * use the controller without linking MuJoCo at all.
 */
#include "wb_character_mj.h"

typedef struct {
    wb_physics *p;
    int body;
} wb_mj_ctx;

static void wb_mj_get_vel(void *ctx, double v[3]) {
    wb_mj_ctx *m = (wb_mj_ctx *)ctx;
    wb_physics_get_vel(m->p, m->body, v);
}
static void wb_mj_set_vel(void *ctx, const double v[3]) {
    wb_mj_ctx *m = (wb_mj_ctx *)ctx;
    wb_physics_character_move(m->p, m->body, v);
}
static void wb_mj_set_vel_direct(void *ctx, const double v[3]) {
    wb_mj_ctx *m = (wb_mj_ctx *)ctx;
    wb_physics_set_vel(m->p, m->body, v);
}
static int wb_mj_grounded(void *ctx) {
    wb_mj_ctx *m = (wb_mj_ctx *)ctx;
    return wb_physics_grounded(m->p, m->body);
}
static void wb_mj_set_yaw(void *ctx, double yaw) {
    wb_mj_ctx *m = (wb_mj_ctx *)ctx;
    wb_physics_set_yaw(m->p, m->body, yaw);
}

void wb_character_apply(wb_character *c, wb_physics *p, int body,
                        const wb_actions *a, const wb_camera *cam, double dt) {
    wb_mj_ctx m = { p, body };
    wb_character_body b = {
        .ctx            = &m,
        .get_vel        = wb_mj_get_vel,
        .set_vel        = wb_mj_set_vel,
        .set_vel_direct = wb_mj_set_vel_direct,
        .grounded       = wb_mj_grounded,
        .set_yaw        = wb_mj_set_yaw
    };
    wb_character_apply_body(c, &b, a, cam, dt);
}
