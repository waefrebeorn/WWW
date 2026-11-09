/*
 * wb_space_bind.c -- the gamemode-facing space bindings (slice 21).
 * See wb_space_bind.h.
 *
 * The engine stays ignorant of any particular game: a gate is just a volume in
 * one world pointing at another gametype. Everything here is a thin binding
 * over wb_worldspace.
 */
#include "wb_space_bind.h"
#include "wb_script.h"
#include "wb_worldspace.h"

#include <lua.h>
#include <lauxlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------- space bindings (slice 21)
 * The Yu-Gi-Oh stadium mechanic, exposed to a gamemode's own script. A mode
 * declares its spawns and its stadium gates in main.lua; travel is triggered
 * from step() when a player stands inside one. The engine stays ignorant of
 * any particular game: a spawn is just a place, a gate is just a volume in one
 * world pointing at another gametype. */

/* wb.add_spawn(x, y, z, team, tag) -> spawn_index
 * Declare a spawn point in the script's own gametype. team -1 = any team;
 * tag names a landing zone (a portal's destination). */
static int l_add_spawn(lua_State *L) {
    wb_worldspace *ws = NULL;
    char self_mode[WB_MODE_NAME_LEN];
    if (wb_script_space_ctx(L, &ws, self_mode, sizeof self_mode) != 0 || !ws) {
        lua_pushinteger(L, -1);
        return 1;
    }
    float pos[3];
    pos[0] = (float)luaL_checknumber(L, 1);
    pos[1] = (float)luaL_checknumber(L, 2);
    pos[2] = (float)luaL_checknumber(L, 3);
    int team = (int)luaL_optinteger(L, 4, -1);
    const char *tag = luaL_optstring(L, 5, "");
    lua_pushinteger(L, wb_worldspace_add_spawn(ws, self_mode, pos, team, tag));
    return 1;
}

/* wb.spawn_count() -> number of spawn points declared in this gametype */
static int l_spawn_count(lua_State *L) {
    wb_worldspace *ws = NULL;
    char self_mode[WB_MODE_NAME_LEN];
    if (wb_script_space_ctx(L, &ws, self_mode, sizeof self_mode) != 0 || !ws) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, wb_worldspace_spawn_count(ws, self_mode));
    return 1;
}

/* wb.add_gate(dst_gamemode, dst_tag, x, y, z, radius) -> gate_index
 * Declare a stadium gate in the script's own gametype. Returns the index, or
 * -1 when the script has no space attached or the destination is empty. */
static int l_add_gate(lua_State *L) {
    wb_worldspace *ws = NULL;
    char self_mode[WB_MODE_NAME_LEN];
    if (wb_script_space_ctx(L, &ws, self_mode, sizeof self_mode) != 0 || !ws) {
        lua_pushinteger(L, -1);
        return 1;
    }
    const char *dst = luaL_checkstring(L, 1);
    const char *tag = luaL_optstring(L, 2, "");
    float pos[3];
    pos[0] = (float)luaL_checknumber(L, 3);
    pos[1] = (float)luaL_checknumber(L, 4);
    pos[2] = (float)luaL_checknumber(L, 5);
    float radius = (float)luaL_optnumber(L, 6, 2.0);
    if (!dst || !dst[0]) { lua_pushinteger(L, -1); return 1; }
    lua_pushinteger(L, wb_worldspace_add_portal(ws, self_mode, pos, radius,
                                                dst, tag));
    return 1;
}

/* wb.gate_count() -> number of gates declared in this gametype */
static int l_gate_count(lua_State *L) {
    wb_worldspace *ws = NULL;
    if (wb_script_space_ctx(L, &ws, NULL, 0) != 0 || !ws) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, wb_worldspace_portal_count(ws));
    return 1;
}

/* wb.gate_at(x, y, z) -> gate_index or nil
 * The walk-in test from script: is this position inside one of this mode's
 * gates? Returns the first matching gate, else nil. */
static int l_gate_at(lua_State *L) {
    wb_worldspace *ws = NULL;
    char self_mode[WB_MODE_NAME_LEN];
    if (wb_script_space_ctx(L, &ws, self_mode, sizeof self_mode) != 0 || !ws) {
        lua_pushnil(L);
        return 1;
    }
    float pos[3];
    pos[0] = (float)luaL_checknumber(L, 1);
    pos[1] = (float)luaL_checknumber(L, 2);
    pos[2] = (float)luaL_checknumber(L, 3);
    for (int i = 0; i < wb_worldspace_portal_count(ws); i++) {
        const wb_worldspace_portal *p = wb_worldspace_get_portal(ws, i);
        if (!p || !p->active || strcmp(p->src_gamemode, self_mode)) continue;
        float dx = p->pos[0] - pos[0], dy = p->pos[1] - pos[1], dz = p->pos[2] - pos[2];
        if (dx * dx + dy * dy + dz * dz <= p->radius * p->radius) {
            lua_pushinteger(L, i);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

/* wb.travel(gate_index, player_id, team, cfg) -> ok, instance, x, y, z
 * The OASIS travel call: the player leaves every world they are in and is
 * spawned into that gate's destination gametype at its landing tag. `cfg` is
 * an optional table of {name=value} for the destination mode's schema. */
static int l_travel(lua_State *L) {
    wb_worldspace *ws = NULL;
    if (wb_script_space_ctx(L, &ws, NULL, 0) != 0 || !ws) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int gate = (int)luaL_checkinteger(L, 1);
    const char *player = luaL_checkstring(L, 2);
    int team = (int)luaL_optinteger(L, 3, -1);
    wb_mode_config cfg;
    wb_mode_config *cfgp = NULL;
    if (lua_istable(L, 4)) {
        memset(&cfg, 0, sizeof cfg);
        int n = 0;
        lua_pushnil(L);
        while (lua_next(L, 4) != 0) {
            if (n < WB_MODE_MAX_CFG_VALUES) {
                const char *k = lua_tostring(L, -2);
                const char *v = lua_tostring(L, -1);
                if (k && v) {
                    snprintf(cfg.values[n].name, sizeof cfg.values[n].name, "%s", k);
                    snprintf(cfg.values[n].value, sizeof cfg.values[n].value, "%s", v);
                    n++;
                }
            }
            lua_pop(L, 1);
        }
        cfg.nvalues = n;
        cfgp = &cfg;
    }
    char inst[WB_DIST_ID_LEN];
    float pos[3];
    char err[512];
    int rc = wb_worldspace_travel(ws, gate, player, cfgp, team, inst, pos,
                                  err, sizeof err);
    if (rc != 0) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, 1);
    lua_pushstring(L, inst);
    lua_pushnumber(L, pos[0]);
    lua_pushnumber(L, pos[1]);
    lua_pushnumber(L, pos[2]);
    return 5;
}

/* wb.instances() -> array of {id=, gamemode=, players=, capacity=, state=}
 * The directory: what worlds exist right now. Read-only, for a host script. */
static int l_instances(lua_State *L) {
    wb_worldspace *ws = NULL;
    if (wb_script_space_ctx(L, &ws, NULL, 0) != 0 || !ws) {
        lua_newtable(L);
        return 1;
    }
    wb_dist_registry *reg = wb_worldspace_registry(ws);
    int n = wb_dist_registry_count(reg);
    wb_dist_info *infos = (wb_dist_info *)calloc((size_t)n, sizeof *infos);
    if (!infos) { lua_newtable(L); return 1; }
    lua_createtable(L, n, 0);
    int got = wb_dist_registry_list(reg, infos, n);
    for (int i = 0; i < got; i++) {
        lua_createtable(L, 0, 5);
        lua_pushstring(L, infos[i].id);        lua_setfield(L, -2, "id");
        lua_pushstring(L, infos[i].gamemode);  lua_setfield(L, -2, "gamemode");
        lua_pushinteger(L, infos[i].players);  lua_setfield(L, -2, "players");
        lua_pushinteger(L, infos[i].capacity); lua_setfield(L, -2, "capacity");
        lua_pushinteger(L, infos[i].state);    lua_setfield(L, -2, "state");
        lua_rawseti(L, -2, i + 1);
    }
    free(infos);
    return 1;
}

static const luaL_Reg kSpaceBindings[] = {
    {"add_spawn",   l_add_spawn},
    {"spawn_count", l_spawn_count},
    {"add_gate",    l_add_gate},
    {"gate_count",  l_gate_count},
    {"gate_at",     l_gate_at},
    {"travel",      l_travel},
    {"instances",   l_instances},
    {NULL, NULL},
};

int wb_space_bind_install(lua_State *L) {
    if (!L) return -1;
    wb_worldspace *ws = NULL;
    if (wb_script_space_ctx(L, &ws, NULL, 0) != 0 || !ws) return 0;

    lua_getglobal(L, "wb");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return -1; }
    for (const luaL_Reg *r = kSpaceBindings; r->name; r++) {
        lua_pushcfunction(L, r->func);
        lua_setfield(L, -2, r->name);
    }
    lua_pop(L, 1);
    return 0;
}
