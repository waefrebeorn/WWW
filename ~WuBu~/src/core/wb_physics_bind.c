/*
 * wb_physics_bind.c -- the ECS <-> physics contract: bind an entity to a body,
 * feed ECS velocities in before a step, write body transforms back after.
 * See wb_physics.h.
 */
#include "wb_physics_internal.h"

#include <stdlib.h>
#include <string.h>

void wb_physics_bind(wb_physics *p, int body, wb_entity e) {
    if (!p) return;
    if (p->nbinds >= p->cap_binds) {
        int nc = p->cap_binds ? p->cap_binds * 2 : 8;
        wb_binding *nb = (wb_binding *)realloc(p->binds, (size_t)nc * sizeof *nb);
        if (!nb) return;
        p->binds = nb;
        p->cap_binds = nc;
    }
    p->binds[p->nbinds].body = body;
    p->binds[p->nbinds].ent  = e;
    p->nbinds++;
}

void wb_physics_sync_in(wb_physics *p, wb_world *w) {
    if (!p || !p->data || !w) return;
    wb_comp_type vel_t = wb_world_find_comp(w, "velocity");
    if (vel_t == (wb_comp_type)-1) return;
    for (int i = 0; i < p->nbinds; i++) {
        if (!wb_entity_valid(w, p->binds[i].ent)) continue;
        float *v = (float *)wb_comp_get(w, p->binds[i].ent, vel_t);
        if (!v) continue;
        double dv[3] = {v[0], v[1], v[2]};
        int dof = wb_physics_body_dof(p->model, p->binds[i].body);
        if (dof < 0) continue;
        /* Only drive horizontal; leave vertical to gravity/contacts. */
        p->data->qvel[dof + 0] = dv[0];
        p->data->qvel[dof + 1] = dv[1];
    }
}

void wb_physics_sync_out(wb_physics *p, wb_world *w) {
    if (!p || !p->data || !w) return;
    wb_comp_type pos_t = wb_world_find_comp(w, "position");
    if (pos_t == (wb_comp_type)-1) return;
    for (int i = 0; i < p->nbinds; i++) {
        if (!wb_entity_valid(w, p->binds[i].ent)) continue;
        float *pos = (float *)wb_comp_get(w, p->binds[i].ent, pos_t);
        if (!pos) continue;
        int body = p->binds[i].body;
        if (body < 0 || body >= p->model->nbody) continue;
        const mjtNum *x = p->data->xpos + 3 * body;
        pos[0] = (float)x[0]; pos[1] = (float)x[1]; pos[2] = (float)x[2];
    }
}

int wb_physics_bind_count(const wb_physics *p) {
    return p ? p->nbinds : 0;
}
