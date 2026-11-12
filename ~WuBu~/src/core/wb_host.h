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
#include <stdint.h>

#include "wb_dist.h"
#include "wb_ecs.h"
#include "wb_character.h"
#include "wb_input.h"
#include "wb_mode.h"
#include "wb_objective.h"
#include "wb_prof.h"
#include "wb_render.h"
#include "wb_worldspace.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The host installs a few closures into the mode's script state. lua_State is
 * only ever handled opaquely here, so it is declared rather than dragging lua.h
 * into everything that includes this header. */
typedef struct lua_State lua_State;

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

/* ------------------------------------------------------------ objective */

/* Install the match objective the mode declared. `rule` is one of
 * WB_OBJ_FIRST_TO / WB_OBJ_TIMED / WB_OBJ_SURVIVE; `limit_s` is the time limit
 * in seconds (0 = none, which only FIRST_TO accepts); `target` is the FIRST_TO
 * score. Returns 0 on success, -1 on refusal (see wb_host_error).
 *
 * A mode with no objective runs until its script says otherwise -- the objective
 * is the match clock, not a replacement for it. */
int wb_host_set_objective(wb_host *h, int rule, uint32_t sides,
                          uint32_t target, double limit_s);

/* Add the objective half of the script's `wb.*` surface. Call after the mode's
 * entry script has run and the player has been spawned. */
void wb_host_install_bindings(lua_State *L, wb_host *h);

int wb_host_score(wb_host *h, uint32_t side, uint32_t points);
int wb_host_death(wb_host *h, uint32_t side);

const wb_objective *wb_host_objective(const wb_host *h);
int  wb_host_objective_state(const wb_host *h);
int  wb_host_objective_winner(const wb_host *h);
uint32_t wb_host_objective_score_of(const wb_host *h, uint32_t side);
double   wb_host_objective_clock(const wb_host *h);
const char *wb_host_objective_state_name(const wb_host *h);

/* Seconds of simulation time since the host opened -- the objective's clock when
 * there is one, else the tick count. */
double   wb_host_elapsed(const wb_host *h);

/* The mode's onboarding line (NULL when it declared none) and whether it should
 * be on screen right now. */
const char *wb_host_hint(const wb_host *h);
int       wb_host_hint_visible(const wb_host *h);

/* The frame profiler (LP-01), covering every host step. Valid for the host's
 * lifetime; the caller must not dispose it. */
wb_prof *wb_host_profiler(wb_host *h);

/* Fold a profiler slot over the frames it has recorded: last, mean, min, max and
 * the p50/p95/p99 hitch view. 0 on success, -1 if the slot is unknown. */
int wb_host_prof_summary(wb_host *h, const char *name, wb_prof_stat *out);

/* Budget overruns since the host opened (LP-05's gate: a slot given a per-frame
 * allowance counts a frame it exceeded it). */
unsigned long wb_host_prof_overruns(const wb_host *h);

/* ---- Input and the player controller ------------------------------------- */

/* The host owns a platform-agnostic input state. A platform layer feeds it
 * through these; with none attached the host runs with no keys held, which is
 * what the headless binary does. */
wb_input *wb_host_input(wb_host *h);

void wb_host_input_key_down(wb_host *h, int code);
void wb_host_input_key_up(wb_host *h, int code);
void wb_host_input_button(wb_host *h, int button, int down);
void wb_host_input_mouse_move(wb_host *h, double dx, double dy);
void wb_host_input_wheel(wb_host *h, double steps);

/* The key map the host translates into actions. Defaults to WASD + Space +
 * Shift, in platform key codes; a platform layer with real codes remaps this. */
typedef struct {
    int fwd, back, left, right, jump, sprint;
} wb_host_keys;

void wb_host_set_keys(wb_host *h, const wb_host_keys *keys);

/* The actions the current input state maps to, the controller's state, and the
 * follow camera. */
void wb_host_actions(const wb_host *h, wb_actions *out);
wb_character *wb_host_character(wb_host *h);
wb_camera *wb_host_camera(wb_host *h);

/* The player's world position, for a renderer or a tool. -1 if unavailable. */
int wb_host_player_pos(const wb_host *h, double p[3]);

/* Render one frame of the mode's world into `fb`.
 *
 * The shipped world is an ECS of bodies, not terrain, so the frame is the entity
 * layer: a marker at every live body's position, projected through the follow
 * camera, with the player drawn brighter and larger. When the world grows
 * terrain its plate goes down first and this stays the layer above it.
 *
 * Returns the number of markers drawn, or -1 on a bad argument. */
int wb_host_render(wb_host *h, wb_framebuffer *fb);

/* The mode's own wb.running flag: a script sets wb.running = false to stop the
 * host. Missing/falsey means keep running. */
int wb_host_running(wb_host *h);

const char *wb_host_error(const wb_host *h);

#ifdef __cplusplus
}
#endif

#endif /* WB_HOST_H */
