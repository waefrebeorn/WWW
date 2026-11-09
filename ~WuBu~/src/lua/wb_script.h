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
#include "wb_mode.h"  /* wb_mode_manifest, wb_mode_config */
#include "wb_worldspace.h"  /* wb_worldspace, wb_worldspace_portal */
struct lua_State;

/* Register the standard component schema on a freshly initialized world
 * (position/velocity/mass/label), matching what the wb.* bindings use. */
void wb_world_setup_comps(wb_world *w);

/* Build a fresh Lua state bound to the world via the registry, with the
 * wb.* bindings (spawn/set_pos/set_vel/get_pos/set_mass/count) installed.
 * When `ws` (a wb_worldspace) and `self_mode` (the name of the gametype this
 * state belongs to) are given, the space-facing wb.* bindings (gates/travel/
 * instances) are installed too. */
struct lua_State *wb_script_create_state(wb_world *w, wb_worldspace *ws,
                                         const char *self_mode);

/* Create a sandboxed Lua state for UNTRUSTED mod code (the Luau model):
 *   - a fresh global table whose __index is a whitelist proxy, so blocked
 *     libraries (os/io/debug/package/load/loadstring/dofile) read as nil
 *   - only the wb.* capability table is exposed
 *   - a debug hook enforcing a per-step instruction budget, so an infinite
 *     loop cannot hang the host
 * `budget` is the instruction count per step; <= 0 disables the hook. */
struct lua_State *wb_script_create_sandbox_state(wb_world *w, long budget);

/* Attach the space (a wb_worldspace) to a state. The state carries an opaque
 * context -- the space, plus `self_mode`, the gametype the script belongs to,
 * so a mode can only declare gates in its own world. `ws`/`self_mode` may be
 * NULL. The wb.* space bindings are installed separately by
 * wb_space_bind_install() (wb_space_bind.c); this file stays free of any
 * worldspace symbol reference. Returns 0 on success, -1 on allocation failure. */
int wb_script_sandbox_attach_space(struct lua_State *L, wb_worldspace *ws,
                                   const char *self_mode);

/* Read the space context back: 0 when one is attached, -1 when there is none.
 * `self_mode` may be NULL. */
int wb_script_space_ctx(struct lua_State *L, wb_worldspace **ws,
                        char *self_mode, size_t self_mode_sz);

/* Install/replace the instruction-budget hook on an existing state. */
void wb_script_set_budget(struct lua_State *L, long budget);

/* Load and run untrusted source under the sandbox. 0 on success, -1 on error. */
int wb_script_run_sandbox(struct lua_State *L, const char *src, size_t len,
                          const char *chunkname);

/* Did the last sandbox run blow its instruction budget? */
int wb_script_budget_exceeded(struct lua_State *L);

/* Read a numeric global from a sandboxed state (tests / tooling). */
int wb_script_get_sandbox_number(struct lua_State *L, const char *name, double *out);

/* Run gamemode frame: call script-global step(dt). 1 = ran, 0 = none,
 * -1 = Lua error (already reported). */
int wb_script_step(struct lua_State *L, float dt);

/* C-side physics integration (velocity advances position). Shared by host and
 * experiences. */
void wb_physics_tick(wb_world *w, float dt);

#endif /* WB_SCRIPT_H */
