/*
 * wb_script.h -- Shared Lua script host entry points (slice 7).
 *
 * Bridges the C11 core to Lua. A gamemode runs as its OWN Lua state bound to
 * its OWN world -- this is the isolation that makes "spawn a game space"
 * work: N instances of a gamemode are N independent Lua VMs + N ECS worlds.
 */
#ifndef WB_SCRIPT_H
#define WB_SCRIPT_H

#include "wb_ecs.h"   /* wb_world */
struct lua_State;

/* Register the standard component schema on a freshly initialized world
 * (position/velocity/mass/label), matching what the wb.* bindings use. */
void wb_world_setup_comps(wb_world *w);

/* Build a fresh Lua state bound to the world via the registry, with the
 * wb.* bindings (spawn/set_pos/set_vel/get_pos/set_mass/count) installed. */
struct lua_State *wb_script_create_state(wb_world *w);

/* Run gamemode frame: call script-global step(dt). 1 = ran, 0 = none,
 * -1 = Lua error (already reported). */
int wb_script_step(struct lua_State *L, float dt);

/* C-side physics integration (velocity advances position). Shared by host and
 * experiences. */
void wb_physics_tick(wb_world *w, float dt);

#endif /* WB_SCRIPT_H */
