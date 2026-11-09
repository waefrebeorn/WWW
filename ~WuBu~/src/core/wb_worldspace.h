/*
 * wb_worldspace.h -- WuBu Engine: the spawn-a-worldspace facade.
 *
 * This is the entry the host calls when a player picks a gametype and asks to
 * be put in a world. It ties together the three things the space knows about:
 *
 *   - gametype manifests   (what a mode is, what config it accepts)
 *   - instances            (wb_dist: a running copy of a mode)
 *   - spawn points         (wb_spawn: where in the world a player appears)
 *
 * The facade knows nothing about any particular game. That is the whole
 * design: pick a mode, get a world.
 *
 * C11, no external deps beyond wb_dist / wb_mode / wb_spawn.
 */
#ifndef WB_WORLDSPACE_H
#define WB_WORLDSPACE_H

#include "wb_dist.h"
#include "wb_mode.h"
#include "wb_spawn.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_worldspace wb_worldspace;

wb_worldspace *wb_worldspace_open(void);
void wb_worldspace_close(wb_worldspace *ws);

/* Register a gametype from a directory that contains mode.json. The mode's
 * entry script resolves against this directory. Returns 0 on success. */
int wb_worldspace_register_mode(wb_worldspace *ws, const char *dir,
                                char *err, size_t err_sz);

int wb_worldspace_mode_count(const wb_worldspace *ws);
const wb_mode_manifest *wb_worldspace_mode(const wb_worldspace *ws, const char *name);

/* The i-th registered gametype (0 <= index < wb_worldspace_mode_count), or
 * NULL. Lets a host walk the registered set -- e.g. to install each mode's
 * declared spawn points. */
const wb_mode_manifest *wb_worldspace_mode_at(const wb_worldspace *ws, int index);

/* The directory a mode was registered from (for resolving its assets). */
int wb_worldspace_mode_dir(const wb_worldspace *ws, const char *name,
                           char *out, size_t out_sz);

/* Declare a spawn point for a gametype. */
int wb_worldspace_add_spawn(wb_worldspace *ws, const char *gamemode,
                            const float pos[3], int team, const char *tag);

int wb_worldspace_spawn_count(const wb_worldspace *ws, const char *gamemode);

/* The instance registry behind the space (tooling, tests, and any host that
 * wants to inspect or drive instances directly). */
wb_dist_registry *wb_worldspace_registry(wb_worldspace *ws);

/* THE call: pick a gametype, get a world.
 *
 *  1. look the gametype up (unknown -> -1)
 *  2. validate `cfg` against its schema (NULL = all defaults)
 *  3. matchmake an instance for that gametype, creating one if none is open
 *  4. pick and reserve a spawn point for (gamemode, team)
 *
 * On success returns 0, `out_instance` holds the instance id and `out_pos`
 * the world position to spawn at. */
int wb_worldspace_spawn(wb_worldspace *ws, const char *gamemode,
                        const wb_mode_config *cfg, const char *player_id,
                        int team, char *out_instance /* WB_DIST_ID_LEN */,
                        float out_pos[3], char *err, size_t err_sz);

/* ------------------------------------------------------------------ portals
 * The Yu-Gi-Oh stadium mechanic: the hub world is a 1:1 mirror-slice of the
 * universe, and a stadium is a game space the player steps INTO. A portal is a
 * volume in a source gamemode that, when a player walks in, detaches them from
 * every world they are in and spawns them into another gamemode's instance at
 * a tagged landing spawn -- the same player identity carried across (the OASIS
 * travel contract). */

#define WB_WORLDSPACE_DST_LEN 128
#define WB_WORLDSPACE_TAG_LEN 64

typedef struct {
    int   active;
    float pos[3];                 /* centre of the gate volume              */
    float radius;                 /* entry radius (walk-in)                 */
    char  src_gamemode[WB_WORLDSPACE_DST_LEN];
    char  dst_gamemode[WB_WORLDSPACE_DST_LEN];
    char  dst_tag[WB_WORLDSPACE_TAG_LEN];   /* landing spawn tag, empty = any */
} wb_worldspace_portal;

/* Declare a stadium gate: players inside this volume of `src_gamemode` travel
 * to `dst_gamemode` (at `dst_tag` when set). Returns the portal index >= 0. */
int wb_worldspace_add_portal(wb_worldspace *ws, const char *src_gamemode,
                             const float pos[3], float radius,
                             const char *dst_gamemode, const char *dst_tag);

int  wb_worldspace_portal_count(const wb_worldspace *ws);
const wb_worldspace_portal *wb_worldspace_get_portal(const wb_worldspace *ws, int index);

/* 1 if `pos` is inside any stadium gate of `src_gamemode` (the walk-in); the
 * first matching portal is copied into `out` (may be NULL). 0 otherwise. */
int wb_worldspace_portal_at(const wb_worldspace *ws, const char *src_gamemode,
                            const float pos[3], wb_worldspace_portal *out);

/* THE travel call: a player standing in stadium portal `portal_index` leaves
 * every instance they are in and is spawned into the portal's destination
 * gamemode at its landing tag. `cfg` NULL = schema defaults. On success
 * returns 0, out_instance = the destination world id, out_pos = the landing. */
int wb_worldspace_travel(wb_worldspace *ws, int portal_index,
                         const char *player_id, const wb_mode_config *cfg,
                         int team, char *out_instance, float out_pos[3],
                         char *err, size_t err_sz);

#ifdef __cplusplus
}
#endif

#endif /* WB_WORLDSPACE_H */
