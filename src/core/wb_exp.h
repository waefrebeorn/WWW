/*
 * wb_exp.h -- Experience container for WuBu Engine (slice 7).
 *
 * A game space. In the Roblox-slerm framing: a gamemode is a Lua script; an
 * EXPERIENCE is one *instantiation* of that gamemode spawned into a tile-cell
 * of the shared world. It owns its own Lua state and ECS (so instances of the
 * same gamemode are fully independent), carries a spatial footprint (the
 * quadkey cell it lives at), and steps each frame.
 *
 * "You spawn in a game space" = you (a player entity) are added to an
 * experience's world, exactly like joining a place.
 */
#ifndef WB_EXP_H
#define WB_EXP_H

#include <stdint.h>
#include <stddef.h>
#include "wb_spatial.h"

typedef struct wb_exp wb_exp;
typedef struct lua_State lua_State;

/* Which source the gamemode script is loaded from (the world anchor cell
 * the experience occupies + the Lua script). */
typedef struct {
    wb_qkey cell;         /* tile cell the arena sits on */
    const char *lua_path; /* gamemode script to instantiate */
} wb_exp_cfg;

/* lifecycle */
wb_exp   *wb_exp_spawn(const wb_exp_cfg *cfg, const char *gamemode_name);
void      wb_exp_dispose(wb_exp *e);

/* query */
const char *wb_exp_name(const wb_exp *e);
wb_qkey     wb_exp_cell(const wb_exp *e);

/* add a player/entity into this experience's world (the "spawn in" entry);
 * returns the entity label as given, or NULL. */
int wb_exp_join(wb_exp *e, const char *player_id);

/* run one frame: step the gamemode's Lua (update) then entity count */
uint32_t wb_exp_step(wb_exp *e, float dt);

/* direct access to the experience's world for engine-side interop */
void *wb_exp_world(wb_exp *e);

#endif /* WB_EXP_H */
