/*
 * wb_physics_world.c -- physics lifecycle and body construction. See
 * wb_physics.h.
 */
#include "wb_physics_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(wb_physics *p, const char *msg) {
    snprintf(p->err, sizeof p->err, "%s", msg ? msg : "unknown physics error");
}

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
