/*
 * wb_physics.c -- WuBu Engine physics world (slice 9). See wb_physics.h.
 *
 * Backed by MuJoCo 4 (Apache-2.0). The model is authored programmatically with
 * mjSpec (no XML files at runtime), compiled once, then stepped. ECS entities
 * bind to MuJoCo bodies by id; physics owns their transforms.
 */
#include "wb_physics.h"

#include <mujoco/mujoco.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* bindings                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    int       body;
    wb_entity ent;
} wb_binding;

struct wb_physics {
    mjSpec    *spec;
    mjModel   *model;
    mjData    *data;
    double     timestep;

    wb_binding *binds;
    int         nbinds;
    int         cap_binds;

    int         next_body_id;   /* MuJoCo ids: world=0, then creation order */

    double     *foot;           /* per-body sole offset (origin -> ground) */
    int         foot_cap;

    char        err[512];
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void set_err(wb_physics *p, const char *msg) {
    snprintf(p->err, sizeof p->err, "%s", msg ? msg : "unknown physics error");
}

/* Find a registered ECS component type id by name; -1 if absent. */
static wb_comp_type find_comp(const wb_world *w, const char *name) {
    for (uint32_t i = 0; i < w->comp_count; i++)
        if (strcmp(w->descs[i].name, name) == 0) return (wb_comp_type)i;
    return (wb_comp_type)-1;
}

/* Body id lookup by name (MuJoCo ids, so the world body 0 is included). */
static int body_id_of(const wb_physics *p, const char *name) {
    if (!p->model) return -1;
    int id = mj_name2id(p->model, mjOBJ_BODY, name);
    return id;
}

/* Record the distance from a body's origin to its sole (for ground checks). */
static void set_foot(wb_physics *p, int body, double offset) {
    if (!p || body < 0) return;
    if (body >= p->foot_cap) {
        int nc = p->foot_cap ? p->foot_cap * 2 : 16;
        if (nc <= body) nc = body + 1;
        double *nf = (double *)realloc(p->foot, (size_t)nc * sizeof *nf);
        if (!nf) return;
        for (int i = p->foot_cap; i < nc; i++) nf[i] = 0.0;
        p->foot = nf;
        p->foot_cap = nc;
    }
    p->foot[body] = offset;
}

/* First joint's dof address for a body, or -1 if it has no joints.
 * For a free-joint body this is the 6-dof root (linear at +0..2, angular +3..5);
 * for the character controller it is the 4-dof root (x, y, z, yaw). */
static int body_dof(const mjModel *m, int body) {
    if (body < 0 || body >= m->nbody) return -1;
    int jnt = m->body_jntadr[body];
    if (jnt < 0 || jnt >= m->njnt) return -1;
    return m->jnt_dofadr[jnt];
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

wb_physics *wb_physics_new(void) {
    wb_physics *p = (wb_physics *)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->spec = mj_makeSpec();
    if (!p->spec) { free(p); return NULL; }
    p->timestep = 0.002;                 /* MuJoCo default */
    p->spec->option.timestep = p->timestep;
    p->spec->option.gravity[2] = -9.81;  /* z-up, SI units */
    p->next_body_id = 1;                 /* body 0 is the world */
    return p;
}

void wb_physics_free(wb_physics *p) {
    if (!p) return;
    if (p->data)  mj_deleteData(p->data);
    if (p->model) mj_deleteModel(p->model);
    if (p->spec)  mj_deleteSpec(p->spec);
    free(p->binds);
    free(p->foot);
    free(p);
}

const char *wb_physics_error(const wb_physics *p) {
    return (p && p->err[0]) ? p->err : NULL;
}

int wb_physics_build(wb_physics *p) {
    if (!p || !p->spec) return -1;
    if (p->model) { set_err(p, "already built"); return -1; }
    mjModel *m = mj_compile(p->spec, NULL);
    if (!m) { set_err(p, "mj_compile failed"); return -1; }
    p->model = m;
    p->data = mj_makeData(m);
    if (!p->data) { set_err(p, "mj_makeData failed"); return -1; }
    mj_forward(m, p->data);
    return 0;
}

/* ------------------------------------------------------------------ */
/* body construction                                                   */
/* ------------------------------------------------------------------ */

/* Internal: add a body, optionally with a free joint. Returns the future
 * MuJoCo body id (>=1) or -1. */
static int add_body_ex(wb_physics *p, int kind, const char *name,
                       const double pos[3], const double size[3], double mass,
                       int free_joint) {
    if (!p || !p->spec) return -1;
    if (p->model) { set_err(p, "cannot add bodies after build"); return -1; }

    mjsBody *world = mjs_findBody(p->spec, "world");
    mjsBody *b = mjs_addBody(world, NULL);
    if (!b) { set_err(p, "mjs_addBody failed"); return -1; }
    if (name) mjs_setName(b->element, name);
    b->pos[0] = pos[0]; b->pos[1] = pos[1]; b->pos[2] = pos[2];

    int dynamic = (mass > 0.0);
    if (dynamic) {
        b->mass = mass;
        if (free_joint) mjs_addFreeJoint(b);
    }

    mjsGeom *g = mjs_addGeom(b, NULL);
    if (!g) { set_err(p, "mjs_addGeom failed"); return -1; }

    switch (kind) {
        case WB_PHYS_PLANE:
            g->type = mjGEOM_PLANE;
            g->size[0] = size[0] > 0 ? size[0] : 10.0;
            g->size[1] = size[1] > 0 ? size[1] : 10.0;
            g->size[2] = size[2] > 0 ? size[2] : 0.1;
            break;
        case WB_PHYS_BOX:
            g->type = mjGEOM_BOX;
            g->size[0] = size[0]; g->size[1] = size[1]; g->size[2] = size[2];
            break;
        case WB_PHYS_SPHERE:
            g->type = mjGEOM_SPHERE;
            g->size[0] = size[0];
            break;
        case WB_PHYS_CAPSULE:
            g->type = mjGEOM_CAPSULE;
            g->size[0] = size[0];           /* radius */
            g->size[1] = size[1] > 0 ? size[1] : size[0];  /* half-height */
            break;
        default:
            set_err(p, "unknown body kind");
            return -1;
    }
    /* Static bodies are welded; dynamic bodies get the mass from the geom. */
    if (dynamic) g->mass = mass;
    else         g->mass = 0.0;

    /* MuJoCo assigns body ids sequentially with the world body at 0, so the
     * id this body will have after compile is the running counter. */
    return p->next_body_id++;
}

int wb_physics_add_body(wb_physics *p, int kind, const char *name,
                        const double pos[3], const double size[3], double mass) {
    return add_body_ex(p, kind, name, pos, size, mass, 1);
}

int wb_physics_add_ground(wb_physics *p, const char *name) {
    if (!p || !p->spec) return -1;
    if (p->model) { set_err(p, "cannot add bodies after build"); return -1; }
    mjsBody *world = mjs_findBody(p->spec, "world");
    if (!world) { set_err(p, "no world body in spec"); return -1; }
    mjsGeom *g = mjs_addGeom(world, NULL);
    if (!g) { set_err(p, "mjs_addGeom(plane) failed"); return -1; }
    g->type = mjGEOM_PLANE;
    g->size[0] = 10.0;   /* rendered grid half-extent */
    g->size[1] = 10.0;
    g->size[2] = 0.1;    /* grid spacing */
    if (name) mjs_setName(g->element, name);
    return 0;
}

int wb_physics_add_character(wb_physics *p, const char *name,
                             const double pos[3], double radius,
                             double height, double mass) {
    double size[3] = {radius, height * 0.5, 0};
    int id = add_body_ex(p, WB_PHYS_CAPSULE, name, pos, size, mass, 0);
    if (id < 0) return -1;

    /* A game character must not tip over. Instead of a free joint (which
     * rolls and falls), give it the classic controller rig: three orthogonal
     * slide joints (x, y, z) plus a yaw hinge. Four dofs, upright by
     * construction, still fully collision-driven. */
    mjsBody *b = mjs_findBody(p->spec, name);
    if (!b) { set_err(p, "character body vanished"); return -1; }

    static const double axes[4][3] = {
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 1}
    };
    static const mjtJoint types[4] = {
        mjJNT_SLIDE, mjJNT_SLIDE, mjJNT_SLIDE, mjJNT_HINGE
    };
    for (int i = 0; i < 4; i++) {
        mjsJoint *j = mjs_addJoint(b, NULL);
        if (!j) { set_err(p, "mjs_addJoint failed"); return -1; }
        j->type = types[i];
        j->axis[0] = axes[i][0];
        j->axis[1] = axes[i][1];
        j->axis[2] = axes[i][2];
    }
    /* Sole offset: distance from the body origin down to the capsule bottom. */
    set_foot(p, id, height * 0.5 + radius);
    return id;
}

/* ------------------------------------------------------------------ */
/* simulation                                                          */
/* ------------------------------------------------------------------ */

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
    if (!p || !p->data || body < 0 || body >= p->model->nbody) return;
    const mjtNum *x = p->data->xpos + 3 * body;
    out[0] = x[0]; out[1] = x[1]; out[2] = x[2];
}

void wb_physics_get_quat(const wb_physics *p, int body, double out[4]) {
    if (!p || !p->data || body < 0 || body >= p->model->nbody) return;
    const mjtNum *q = p->data->xquat + 4 * body;
    out[0] = q[0]; out[1] = q[1]; out[2] = q[2]; out[3] = q[3];
}

void wb_physics_set_vel(wb_physics *p, int body, const double vel[3]) {
    if (!p || !p->data) return;
    int dof = body_dof(p->model, body);
    if (dof < 0) return;
    p->data->qvel[dof + 0] = vel[0];
    p->data->qvel[dof + 1] = vel[1];
    p->data->qvel[dof + 2] = vel[2];
}

void wb_physics_get_vel(const wb_physics *p, int body, double out[3]) {
    if (!p || !p->data) { out[0]=out[1]=out[2]=0; return; }
    int dof = body_dof(p->model, body);
    if (dof < 0) { out[0]=out[1]=out[2]=0; return; }
    out[0] = p->data->qvel[dof + 0];
    out[1] = p->data->qvel[dof + 1];
    out[2] = p->data->qvel[dof + 2];
}

void wb_physics_character_move(wb_physics *p, int body, const double vel[3]) {
    /* Drive horizontal velocity; preserve whatever vertical velocity gravity
     * and contacts have produced, so the character still falls and lands. */
    if (!p || !p->data) return;
    int dof = body_dof(p->model, body);
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
        double pos[3]; wb_physics_get_pos(p, body, pos);
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

/* ------------------------------------------------------------------ */
/* ECS binding                                                         */
/* ------------------------------------------------------------------ */

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
    wb_comp_type vel_t = find_comp(w, "velocity");
    if (vel_t == (wb_comp_type)-1) return;
    for (int i = 0; i < p->nbinds; i++) {
        if (!wb_entity_valid(w, p->binds[i].ent)) continue;
        float *v = (float *)wb_comp_get(w, p->binds[i].ent, vel_t);
        if (!v) continue;
        double dv[3] = {v[0], v[1], v[2]};
        int dof = body_dof(p->model, p->binds[i].body);
        if (dof < 0) continue;
        /* Only drive horizontal; leave vertical to gravity/contacts. */
        p->data->qvel[dof + 0] = dv[0];
        p->data->qvel[dof + 1] = dv[1];
    }
}

void wb_physics_sync_out(wb_physics *p, wb_world *w) {
    if (!p || !p->data || !w) return;
    wb_comp_type pos_t = find_comp(w, "position");
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
