/*
 * wb_host.h -- the gametype host (slice 22).
 *
 * The engine binary's actual job: point it at a directory of gametypes, name
 * one, and it puts a player into that mode's world and runs the mode's script.
 * This is the first PRODUCTION caller of the spawn-a-worldspace facade --
 * until now wb_worldspace_open/spawn existed only in tests.
 *
 *   wubu_engine.exe <modes-root> <gamemode> [player-id]
 *
 * `modes_root` is a directory of gametype packages (the doctrine: "gametypes
 * are files in a directory you own"). Every immediate subdirectory holding a
 * mode.json is registered, so a mode's stadium gates can name any sibling.
 * The named mode's entry script is then run -- it declares its own spawn
 * points (wb.add_spawn) and stadiums (wb.add_gate) -- and the player is
 * spawned into the mode's world at one of those points.
 *
 * C11. The Lua host is the vendored Lua 5.4.
 */
#ifndef WB_HOST_H
#define WB_HOST_H

#include <stddef.h>

#include "wb_ecs.h"
#include "wb_mode.h"
#include "wb_worldspace.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_host wb_host;

/* Open a world space, register every gametype under `modes_root`, run
 * `gamemode`'s entry script with the space bindings installed and attributed
 * to that mode, then spawn `player_id` into it.
 *
 * Returns NULL on failure; `err` (if not NULL) receives the reason.
 * `team` is the player's team (-1 = any). */
wb_host *wb_host_open(const char *modes_root, const char *gamemode,
                      const char *player_id, int team,
                      char *err, size_t err_sz);

void wb_host_close(wb_host *h);

/* The gametype the player spawned into, and the instance they are in. */
const char *wb_host_mode(const wb_host *h);
const char *wb_host_instance(const wb_host *h);

/* The world the mode is running in (tooling / the main loop). */
wb_world *wb_host_world(wb_host *h);

/* The world space behind the host (tooling / tests). */
wb_worldspace *wb_host_space(wb_host *h);

/* The player's entity handle in the mode's world (also exposed to the mode's
 * script as wb.player). */
wb_entity wb_host_player(const wb_host *h);

/* The simulation's fixed step. The host consumes whole steps from the frame
 * time the caller passes and carries the remainder, so the sim rate does not
 * depend on how the caller paces itself -- LP-03/LP-04's contract. */
#define WB_HOST_FIXED_DT (1.0 / 60.0)

/* Run one frame of the mode. `dt` is the elapsed frame time; the host advances
 * its fixed-step accumulator by it and runs however many whole steps that
 * buys, integrating physics and refreshing the digest once per step.
 * Returns 1 if at least one step ran, 0 if the frame bought no whole step,
 * -1 on error (see wb_host_error). */
int wb_host_step(wb_host *h, float dt);

/* Whole simulation steps taken so far. */
uint32_t wb_host_ticks(const wb_host *h);

/* The per-tick divergence digest over the simulation state (position +
 * velocity), LP-03's detector: two runs built and stepped the same way agree
 * bit-for-bit, and a diverged one does not. */
uint64_t wb_host_digest(const wb_host *h);

/* Render-interpolation alpha in [0,1) from the accumulator's remainder. The sim
 * state is never interpolated -- only what a renderer would draw. */
float wb_host_render_alpha(const wb_host *h);

/* The mode's own wb.running flag: a script sets wb.running = false to stop the
 * host. Missing/falsey means keep running. */
int wb_host_running(wb_host *h);

const char *wb_host_error(const wb_host *h);

#ifdef __cplusplus
}
#endif

#endif /* WB_HOST_H */
