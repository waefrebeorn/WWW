/*
 * wb_physics_body.c -- per-body queries and control: step, transforms,
 * velocities, grounding, facing. See wb_physics.h.
 */
#include "wb_physics_internal.h"

#include <stdlib.h>

int wb_physics_body_dof(const mjModel *m, int body) {
    if (!m || body < 0 || body >= m->nbody) return -1;
    int jnt = m->body_jntadr[body];
    if (jnt < 0 || jnt >= m->njnt) return -1;
    return m->jnt_dofadr[jnt];
}

/* Body id lookup by name (MuJoCo ids, so the world body 0 is included). */
static int body_id_of(const wb_physics *p, const char *name) {
    if (!p->model) return -1;
    return mj_name2id(p->model, mjOBJ_BODY, name);
}

void wb_physics_step(wb_physics *p, double dt) {
    if (!p || !p->model || !p->data) return;
    if (dt > 0) {
        p->timestep = dt;
        p->model->opt.timestep = dt;
    }
    mj_step(p->model, p->data);
}

int wb_physics_body_count(const wb_physics *p) {
    return (p && p->model) ? p->model->nbody : 0;
}

int wb_physics_find(const wb_physics *p, const char *name) {
    return (p && p->model) ? body_id_of(p, name) : -1;
}

void wb_physics_get_pos(const wb_physics *p, int body, double out[3]) {
    if (!p || !p->data || !p->model) return;
    if (body < 0 || body >= p->model->nbody) return;
    const mjtNum *x = p->data->xpos + 3 * body;
    out[0] = x[0]; out[1] = x[1]; out[2] = x[2];
}

void wb_physics_get_quat(const wb_physics *p, int body, double out[4]) {
    if (!p || !p->data || !p->model) return;
    if (body < 0 || body >= p->model->nbody) return;
    const mjtNum *q = p->data->xquat + 4 * body;
    out[0] = q[0]; out[1] = q[1]; out[2] = q[2]; out[3] = q[3];
}

void wb_physics_set_vel(wb_physics *p, int body, const double vel[3]) {
    if (!p || !p->data) return;
    int dof = wb_physics_body_dof(p->model, body);
    if (dof < 0) return;
    p->data->qvel[dof + 0] = vel[0];
    p->data->qvel[dof + 1] = vel[1];
    p->data->qvel[dof + 2] = vel[2];
}

void wb_physics_get_vel(const wb_physics *p, int body, double out[3]) {
    out[0] = out[1] = out[2] = 0.0;
    if (!p || !p->data) return;
    int dof = wb_physics_body_dof(p->model, body);
    if (dof < 0) return;
    out[0] = p->data->qvel[dof + 0];
    out[1] = p->data->qvel[dof + 1];
    out[2] = p->data->qvel[dof + 2];
}

void wb_physics_character_move(wb_physics *p, int body, const double vel[3]) {
    /* Drive horizontal velocity; preserve whatever vertical velocity gravity
     * and contacts have produced, so the character still falls and lands. */
    if (!p || !p->data) return;
    int dof = wb_physics_body_dof(p->model, body);
    if (dof < 0) return;
    p->data->qvel[dof + 0] = vel[0];
    p->data->qvel[dof + 1] = vel[1];
}

int wb_physics_grounded(wb_physics *p, int body) {
    if (!p || !p->data || !p->model) return 0;

    /* Fast path: a contact whose normal is roughly vertical. */
    for (int i = 0; i < p->data->ncon; i++) {
        const mjContact *c = &p->data->contact[i];
        int b1 = p->model->geom_bodyid[c->geom1];
        int b2 = p->model->geom_bodyid[c->geom2];
        if (b1 != body && b2 != body) continue;
        double nz = c->frame[2];
        if (nz > 0.5 || nz < -0.5) return 1;
    }

    /* Robust path: cast a ray straight down from the body origin. If it hits
     * within (sole offset + a small margin), the body is standing on it. This
     * stays true while a contact solver micro-bounces the body, which a pure
     * contact-normal test misses. */
    if (p->foot && body < p->foot_cap && p->foot[body] > 0.0) {
        double pos[3];
        wb_physics_get_pos(p, body, pos);
        double dir[3] = {0.0, 0.0, -1.0};
        int gid = -1;
        double d = mj_ray(p->model, p->data, pos, dir, NULL, 1, body, &gid, NULL);
        if (d >= 0.0 && d <= p->foot[body] + 0.06) return 1;
    }
    return 0;
}

void wb_physics_set_yaw(wb_physics *p, int body, double yaw) {
    if (!p || !p->data || !p->model) return;
    if (body < 0 || body >= p->model->nbody) return;
    int j0 = p->model->body_jntadr[body];
    int n  = p->model->body_jntnum[body];
    for (int i = 0; i < n; i++) {
        int j = j0 + i;
        if (j < 0 || j >= p->model->njnt) continue;
        if (p->model->jnt_type[j] == mjJNT_HINGE) {
            int adr = p->model->jnt_qposadr[j];
            if (adr >= 0) p->data->qpos[adr] = yaw;
            return;
        }
    }
}
