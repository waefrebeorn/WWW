/*
 * wb_physics_internal.h -- private state of the physics module.
 *
 * Included only by the wb_physics*.c translation units. The public header
 * keeps `wb_physics` opaque; this is where the struct lives.
 */
#ifndef WB_PHYSICS_INTERNAL_H
#define WB_PHYSICS_INTERNAL_H

#include "wb_physics.h"

#include <mujoco/mujoco.h>

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

/* First joint's dof address for a body, or -1 if it has no joints.
 * For a free-joint body this is the 6-dof root (linear at +0..2, angular +3..5);
 * for the character controller it is the 4-dof root (x, y, z, yaw). */
int wb_physics_body_dof(const mjModel *m, int body);

#endif /* WB_PHYSICS_INTERNAL_H */
