/*
 * wb_physics.h -- WuBu Engine physics world (slice 9).
 *
 * WHY: the kernel had position/velocity/mass but nothing integrated against
 * world geometry -- no collision, no character controller. This is the pillar
 * that makes the world *physical*. Backed by MuJoCo 4 (Apache-2.0, C API,
 * vendored at thirdparty/mujoco): rigid bodies, native convex collision,
 * speculative contacts, soft bodies, and a real solver -- instead of a
 * hand-rolled integrator that would take years to trust.
 *
 * CONTRACT: physics owns the authoritative transform for every bound entity.
 * The ECS holds the game-logic view; `sync_out` writes body transforms back
 * into ECS position components after each step, `sync_in` feeds ECS velocities
 * into the solver before each step (for kinematic/character control).
 *
 * C11, links against vendored MuJoCo (thirdparty/mujoco/lib/mujoco.lib +
 * bin/mujoco.dll). No other deps.
 */
#ifndef WB_PHYSICS_H
#define WB_PHYSICS_H

#include "wb_ecs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_physics wb_physics;

/* Body shapes. */
enum {
    WB_PHYS_PLANE = 0,
    WB_PHYS_BOX,
    WB_PHYS_SPHERE,
    WB_PHYS_CAPSULE
};

/* Lifecycle ------------------------------------------------------------- */

/* Create an empty physics world (gravity on, MuJoCo default 0.002s step).
 * Returns NULL on allocation failure. */
wb_physics *wb_physics_new(void);

/* Destroy the world, its model/data, and all bindings. */
void wb_physics_free(wb_physics *p);

/* Compile everything added so far into a live model. Call once, before the
 * first step. Returns 0 on success, -1 on a MuJoCo compile error (message in
 * wb_physics_error). */
int  wb_physics_build(wb_physics *p);

/* Last error message (compile or runtime), or NULL. */
const char *wb_physics_error(const wb_physics *p);

/* Body construction (before build) -------------------------------------- */

/* Add a body. `size` is shape-specific:
 *   PLANE   : size[0..1] = rendered grid half-extents (physics is infinite)
 *   BOX     : half-extents (x,y,z)
 *   SPHERE  : radius in size[0]
 *   CAPSULE : radius in size[0], half-height in size[1]
 * `mass` <= 0 makes the body static (welded to the world).
 * Returns the body id (>=0) or -1 on failure. */
int wb_physics_add_body(wb_physics *p, int kind, const char *name,
                        const double pos[3], const double size[3], double mass);

/* Convenience: a static ground plane at y-up origin. */
int wb_physics_add_ground(wb_physics *p, const char *name);

/* A velocity-driven capsule character (free joint). `height` is the cylinder
 * section's full height; total height = height + 2*radius. */
int wb_physics_add_character(wb_physics *p, const char *name,
                             const double pos[3], double radius,
                             double height, double mass);

/* Simulation ------------------------------------------------------------ */

/* Step the world by `dt` seconds (sets the MuJoCo timestep). */
void wb_physics_step(wb_physics *p, double dt);

/* Number of bodies in the compiled model (body 0 is always the world). */
int  wb_physics_body_count(const wb_physics *p);

/* Look up a body id by name; -1 if absent. */
int  wb_physics_find(const wb_physics *p, const char *name);

/* Read a body's world position / orientation (quaternion x,y,z,w). */
void wb_physics_get_pos(const wb_physics *p, int body, double out[3]);
void wb_physics_get_quat(const wb_physics *p, int body, double out[4]);

/* Set a body's linear velocity directly (solver still resolves contacts). */
void wb_physics_set_vel(wb_physics *p, int body, const double vel[3]);
void wb_physics_get_vel(const wb_physics *p, int body, double out[3]);

/* Character control: drive a capsule with a desired linear velocity.
 * Vertical velocity is preserved (gravity keeps working). */
void wb_physics_character_move(wb_physics *p, int body, const double vel[3]);

/* True if the body is resting on something (a contact whose normal is
 * within ~60 deg of vertical). Used for jump grounding. */
int  wb_physics_grounded(wb_physics *p, int body);

/* Set a character's facing by writing its hinge joint angle directly.
 * No-op for bodies without a hinge. */
void wb_physics_set_yaw(wb_physics *p, int body, double yaw);

/* ECS binding ----------------------------------------------------------- */

/* Bind an ECS entity to a physics body. Physics then owns that entity's
 * transform: sync_out overwrites the entity's position component. */
void wb_physics_bind(wb_physics *p, int body, wb_entity e);

/* ECS velocity -> solver (call before step, for characters/kinematics). */
void wb_physics_sync_in(wb_physics *p, wb_world *w);

/* solver transforms -> ECS position (call after step). */
void wb_physics_sync_out(wb_physics *p, wb_world *w);

/* Number of active bindings. */
int  wb_physics_bind_count(const wb_physics *p);

#ifdef __cplusplus
}
#endif

#endif /* WB_PHYSICS_H */
